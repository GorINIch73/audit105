#pragma once

#include <string>
#include <vector>

struct Cell {
    int col = 0;
    int span = 1;
    int repeated = 1;
    std::string text;
    double value = 0.0;
    bool hasValue = false;
    bool covered = false;
};

struct Row {
    int index = 0;
    int width = 0;
    std::vector<Cell> cells;

    const Cell* CellAt(int col) const {
        for (const Cell& c : cells) {
            if (c.col <= col && col < c.col + c.span) return &c;
        }
        return nullptr;
    }

    std::string TextAt(int col) const {
        const Cell* c = CellAt(col);
        return c ? c->text : std::string();
    }
};

struct Spreadsheet {
    std::string sheetName;
    std::string source;
    std::string format;
    std::vector<Row> rows;

    bool Empty() const { return rows.empty(); }
    int RowCount() const { return static_cast<int>(rows.size()); }
};
