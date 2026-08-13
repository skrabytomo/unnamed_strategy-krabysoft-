#!/usr/bin/env bash
cd "$(dirname "$0")"
python3 apply_patches_v3.py --dry-run
read -p "Apply for real? (y/n): " CONFIRM
if [ "$CONFIRM" == "y" ]; then python3 apply_patches_v3.py; fi
