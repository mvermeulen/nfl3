#include "CsvParser.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>

CsvParser::Table CsvParser::parse(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open CSV file: " + filename);
    }

    Table result;
    std::vector<std::string> headers;
    std::string line;
    int lineNum = 0;

    while (std::getline(file, line)) {
        ++lineNum;

        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Skip empty lines
        if (line.empty()) continue;

        auto fields = parseLine(line);

        // First line: headers
        if (lineNum == 1) {
            headers = fields;
            continue;
        }

        // Validate field count matches header count
        if (fields.size() != headers.size()) {
            throw std::runtime_error("CSV line " + std::to_string(lineNum) +
                                     " has " + std::to_string(fields.size()) +
                                     " fields but expected " +
                                     std::to_string(headers.size()));
        }

        // Create row map
        Row row;
        for (size_t i = 0; i < headers.size(); ++i) {
            row[headers[i]] = fields[i];
        }
        result.push_back(row);
    }

    file.close();
    return result;
}

void CsvParser::write(const std::string& filename,
                      const std::vector<std::string>& headers,
                      const Table& table) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open CSV file for writing: " + filename);
    }

    // Write headers
    for (size_t i = 0; i < headers.size(); ++i) {
        if (i > 0) file << ",";
        file << quoteField(headers[i]);
    }
    file << "\n";

    // Write rows
    for (const auto& row : table) {
        for (size_t i = 0; i < headers.size(); ++i) {
            if (i > 0) file << ",";
            auto it = row.find(headers[i]);
            if (it != row.end()) {
                file << quoteField(it->second);
            } else {
                file << "\"\"";
            }
        }
        file << "\n";
    }

    file.close();
}

std::vector<std::string> CsvParser::parseLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;
    size_t i = 0;

    while (i < line.length()) {
        char c = line[i];

        if (inQuotes) {
            if (c == '"') {
                // Check for escaped quote (two quotes in a row)
                if (i + 1 < line.length() && line[i + 1] == '"') {
                    field += '"';
                    i += 2;
                    continue;
                } else {
                    // End of quoted field
                    inQuotes = false;
                    i++;
                    continue;
                }
            } else {
                field += c;
                i++;
            }
        } else {
            if (c == '"') {
                // Start of quoted field
                inQuotes = true;
                i++;
            } else if (c == ',') {
                // Field delimiter
                fields.push_back(field);
                field.clear();
                i++;
            } else {
                field += c;
                i++;
            }
        }
    }

    // Add last field
    fields.push_back(field);
    return fields;
}

std::string CsvParser::unquoteField(const std::string& field) {
    if (field.length() < 2 || field.front() != '"' || field.back() != '"') {
        return field;  // Not quoted
    }

    std::string unquoted = field.substr(1, field.length() - 2);
    // Replace escaped quotes with single quotes
    std::string result;
    for (size_t i = 0; i < unquoted.length(); ++i) {
        if (unquoted[i] == '"' && i + 1 < unquoted.length() && unquoted[i + 1] == '"') {
            result += '"';
            i++;  // Skip the next quote
        } else {
            result += unquoted[i];
        }
    }
    return result;
}

std::string CsvParser::quoteField(const std::string& field) {
    // Quote field if it contains comma, quote, or newline
    if (field.find(',') != std::string::npos ||
        field.find('"') != std::string::npos ||
        field.find('\n') != std::string::npos) {
        std::string quoted = "\"";
        for (char c : field) {
            if (c == '"') {
                quoted += "\"\"";  // Escape quotes
            } else {
                quoted += c;
            }
        }
        quoted += "\"";
        return quoted;
    }
    return field;
}
