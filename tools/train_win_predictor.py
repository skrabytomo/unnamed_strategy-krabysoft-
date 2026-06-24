#!/usr/bin/env python3
"""
Train a small MLP to predict win probability from per-turn game state snapshots.
Input:  turn_snapshots joined with matches from fullgame_results.db
Output: win_predictor.npz  (weights/biases for inference in C++)

Usage:
    python3 tools/train_win_predictor.py [--db fullgame_results.db] [--out win_predictor.npz]

Architecture: 11 inputs -> 64 -> 32 -> 1 (sigmoid)
Inputs (all normalised to ~[0,1]):
    f1_idx/8, f2_idx/8, week/30,
    p1_gold/5000, p1_other/500,
    p2_gold/5000, p2_other/500,
    p1_strength/50000, p2_strength/50000,
    strength_ratio (p1/(p1+p2)),
    gold_ratio    (p1_gold/(p1_gold+p2_gold))
Target: 1 if winner==1 else 0  (per snapshot row)
"""

import sqlite3, argparse, sys
import numpy as np

# ── RNG ───────────────────────────────────────────────────────────────────────
RNG = np.random.default_rng(42)

# ── Activations ──────────────────────────────────────────────────────────────
def sigmoid(x):   return 1.0 / (1.0 + np.exp(-np.clip(x, -50, 50)))
def relu(x):      return np.maximum(0, x)
def relu_grad(x): return (x > 0).astype(np.float32)

# ── MLP ───────────────────────────────────────────────────────────────────────
class MLP:
    def __init__(self, layer_sizes):
        self.weights, self.biases = [], []
        for i in range(len(layer_sizes) - 1):
            fan_in = layer_sizes[i]
            fan_out = layer_sizes[i + 1]
            std = np.sqrt(2.0 / fan_in)  # He init
            self.weights.append(RNG.normal(0, std, (fan_in, fan_out)).astype(np.float32))
            self.biases.append(np.zeros(fan_out, dtype=np.float32))

    def forward(self, X):
        self._cache = [X]
        A = X
        for i, (W, b) in enumerate(zip(self.weights, self.biases)):
            Z = A @ W + b
            A = sigmoid(Z) if i == len(self.weights) - 1 else relu(Z)
            self._cache.append((Z, A))
        return A

    def backward(self, X, y, lr):
        n = X.shape[0]
        # Output layer gradient (BCE loss)
        A_out = self._cache[-1][1]
        dA = (A_out - y.reshape(-1, 1)) / n

        for i in reversed(range(len(self.weights))):
            Z, A = self._cache[i + 1]
            if i == len(self.weights) - 1:
                dZ = dA  # sigmoid + BCE combines cleanly
            else:
                dZ = dA * relu_grad(Z)
            A_prev = self._cache[i] if i == 0 else self._cache[i][1]
            dW = A_prev.T @ dZ
            db = dZ.sum(axis=0)
            dA = dZ @ self.weights[i].T
            # Adam would be better but SGD+momentum is enough here
            self.weights[i] -= lr * dW
            self.biases[i]  -= lr * db

    def predict(self, X):
        return self.forward(X)

    def save(self, path):
        arrays = {}
        for i, (W, b) in enumerate(zip(self.weights, self.biases)):
            arrays[f'W{i}'] = W
            arrays[f'b{i}'] = b
        np.savez(path, **arrays)
        print(f"Saved model to {path}")

    @classmethod
    def load(cls, path):
        data = np.load(path)
        n_layers = sum(1 for k in data if k.startswith('W'))
        # reconstruct sizes
        sizes = [data['W0'].shape[0]] + [data[f'W{i}'].shape[1] for i in range(n_layers)]
        mlp = cls(sizes)
        for i in range(n_layers):
            mlp.weights[i] = data[f'W{i}']
            mlp.biases[i]  = data[f'b{i}']
        return mlp

# ── Data loading ──────────────────────────────────────────────────────────────
def load_data(db_path):
    con = sqlite3.connect(db_path)
    cur = con.cursor()

    # Check we have snapshots
    cur.execute("SELECT COUNT(*) FROM turn_snapshots")
    n_snaps = cur.fetchone()[0]
    if n_snaps == 0:
        print("ERROR: turn_snapshots is empty. Re-run sim with --snapshots flag.")
        con.close()
        sys.exit(1)
    print(f"Loaded {n_snaps} snapshots from {db_path}")

    rows = cur.execute("""
        SELECT m.f1, m.f2, m.winner,
               s.week, s.p1_gold, s.p1_other, s.p2_gold, s.p2_other,
               s.p1_strength, s.p2_strength
        FROM turn_snapshots s
        JOIN matches m ON s.match_id = m.id
        WHERE m.winner != 0
    """).fetchall()
    con.close()

    if not rows:
        print("ERROR: No rows returned. Check that matches have winner != 0.")
        sys.exit(1)

    X_list, y_list = [], []
    for (f1, f2, winner, week, p1g, p1o, p2g, p2o, p1s, p2s) in rows:
        total_s = p1s + p2s + 1e-6
        total_g = p1g + p2g + 1e-6
        feats = np.array([
            f1 / 8.0,
            f2 / 8.0,
            week / 30.0,
            p1g / 5000.0,
            p1o / 500.0,
            p2g / 5000.0,
            p2o / 500.0,
            p1s / 50000.0,
            p2s / 50000.0,
            p1s / total_s,
            p1g / total_g,
        ], dtype=np.float32)
        X_list.append(feats)
        y_list.append(1.0 if winner == 1 else 0.0)

    X = np.stack(X_list)
    y = np.array(y_list, dtype=np.float32)
    return X, y

# ── Training ──────────────────────────────────────────────────────────────────
def train(X, y, epochs=200, batch_size=512, lr=1e-3, val_split=0.1):
    n = len(X)
    idx = RNG.permutation(n)
    val_n = max(1, int(n * val_split))
    val_idx, train_idx = idx[:val_n], idx[val_n:]
    X_train, y_train = X[train_idx], y[train_idx]
    X_val,   y_val   = X[val_idx],   y[val_idx]

    mlp = MLP([11, 64, 32, 1])

    best_val_loss = float('inf')
    best_weights  = None

    for epoch in range(1, epochs + 1):
        perm = RNG.permutation(len(X_train))
        X_train, y_train = X_train[perm], y_train[perm]

        for start in range(0, len(X_train), batch_size):
            Xb = X_train[start:start + batch_size]
            yb = y_train[start:start + batch_size]
            mlp.forward(Xb)
            mlp.backward(Xb, yb, lr)

        # Validation loss (BCE)
        pred_val = mlp.predict(X_val).flatten()
        eps = 1e-7
        val_loss = -np.mean(y_val * np.log(pred_val + eps) +
                            (1 - y_val) * np.log(1 - pred_val + eps))
        val_acc  = np.mean((pred_val > 0.5) == y_val)

        if val_loss < best_val_loss:
            best_val_loss = val_loss
            best_weights  = [(W.copy(), b.copy()) for W, b in zip(mlp.weights, mlp.biases)]

        if epoch % 20 == 0 or epoch == 1:
            print(f"  Epoch {epoch:3d}/{epochs}  val_loss={val_loss:.4f}  val_acc={val_acc:.3f}")

        # LR decay
        if epoch == 100:
            lr *= 0.3

    # Restore best
    for i, (W, b) in enumerate(best_weights):
        mlp.weights[i] = W
        mlp.biases[i]  = b

    return mlp

# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--db',  default='fullgame_results.db')
    ap.add_argument('--out', default='win_predictor.npz')
    ap.add_argument('--epochs', type=int, default=200)
    args = ap.parse_args()

    print(f"Loading data from {args.db}...")
    X, y = load_data(args.db)
    print(f"Dataset: {len(X)} samples, {y.mean()*100:.1f}% player-1 wins\n")

    print("Training MLP (11 -> 64 -> 32 -> 1)...")
    mlp = train(X, y, epochs=args.epochs)

    mlp.save(args.out)

    # Quick sanity: overall accuracy
    pred = mlp.predict(X).flatten()
    acc  = np.mean((pred > 0.5) == y)
    print(f"\nFull-set accuracy: {acc*100:.1f}%")
    print("Done. Load win_predictor.npz in C++ or Python for inference.")

if __name__ == '__main__':
    main()
