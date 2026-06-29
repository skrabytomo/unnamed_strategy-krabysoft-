#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct sqlite3;

struct SaveEntry {
    int64_t     id          = 0;
    std::string name;           // user-visible save name
    std::string heroName;
    std::string factionName;
    int         day         = 0;
    int         week        = 0;
    bool        isCampaign  = false;
    int         missionIdx  = 0;
    int64_t     updatedAt   = 0; // unix timestamp
};

class SaveDB {
public:
    SaveDB() = default;
    ~SaveDB();

    bool open(const std::string& path);
    void close();
    bool isOpen() const { return m_db != nullptr; }

    // Upsert: if id==0 inserts new row, else updates existing. Returns row id (>0) or 0 on error.
    int64_t upsert(int64_t id,
                   const std::string& name,
                   const std::string& saveJson,
                   bool isCampaign,
                   int missionIdx,
                   const std::string& heroName,
                   const std::string& factionName,
                   int day, int week);

    bool    load(int64_t id, std::string& jsonOut) const;
    bool    del(int64_t id);
    bool    rename(int64_t id, const std::string& newName);

    std::vector<SaveEntry> list(bool campaignOnly) const;
    std::vector<SaveEntry> listAll() const;

private:
    bool createSchema();
    bool execSQL(const char* sql) const;
    sqlite3* m_db = nullptr;
};
