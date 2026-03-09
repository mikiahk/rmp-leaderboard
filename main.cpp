#include <iostream>
#include <curl/curl.h>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ─── libcurl write callback ───────────────────────────────────────────────────
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

// ─── Shared POST helper ───────────────────────────────────────────────────────
static std::string postGraphQL(const std::string& body) {
    CURL* curl = curl_easy_init();
    std::string response;

    if (!curl) {
        std::cerr << "Failed to initialize curl\n";
        return "";
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0");

    curl_easy_setopt(curl, CURLOPT_URL, "https://www.ratemyprofessors.com/graphql");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "Curl error: " << curl_easy_strerror(res) << "\n";
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

// ─── Fetch school ID ──────────────────────────────────────────────────────────
std::string fetchSchoolId(const std::string& schoolName) {
    std::string safe = schoolName;
    const std::string body =
        "{\"query\":\"query NewSearchSchoolsQuery(\\n  $query: SchoolSearchQuery!\\n) {\\n  newSearch {\\n    schools(query: $query) {\\n      edges {\\n        cursor\\n        node {\\n          id\\n          legacyId\\n          name\\n          city\\n          state\\n          numRatings\\n          avgRatingRounded\\n        }\\n      }\\n      pageInfo {\\n        hasNextPage\\n        endCursor\\n      }\\n    }\\n  }\\n}\","
        "\"variables\":{\"query\":{\"text\":\"" + safe + "\"}}}";
    return postGraphQL(body);
}

// ─── Parse school ID ──────────────────────────────────────────────────────────
std::string parseSchoolId(const std::string& jsonStr) {
    try {
        auto j = json::parse(jsonStr);
        auto& edges = j["data"]["newSearch"]["schools"]["edges"];
        if (edges.empty()) return "";

        auto& node = edges[0]["node"];

        // Print the best match so the user can verify
        std::cout << "   📍 " << node.value("name", "?")
                  << " - " << node.value("city", "?")
                  << ", " << node.value("state", "?")
                  << " (" << node.value("numRatings", 0) << " ratings)\n";

        // legacyId is a plain integer — required for the teacher search query
        // std::cout << node["id"] << std::endl;
        return node["id"].get<std::string>();
        // return std::to_string(node["legacyId"].get<int>());

    } catch (const std::exception& e) {
        std::cerr << "❌ School parse error: " << e.what() << "\n";
        std::cerr << "   Raw response: " << jsonStr.substr(0, 200) << "\n";
        return "";
    }
}

struct Professor {
    std::string firstName;
    std::string lastName;
    std::string department;
    float avgRating      = 0.0f;
    float avgDifficulty  = 0.0f;
    float wouldTakeAgain = 0.0f;
    int   numRatings     = 0;
};

// ─── Fetch and Parse professors ─────────────────────────────────────────────────────────
std::vector<Professor> fetchAndParseProfessors(const std::string& schoolId) {
    std::vector<Professor> professors;
    std::string cursor = "";
    bool hasNextPage = true;

    while (hasNextPage) {
        std::string afterClause = cursor.empty() ? "" : ",\"after\":\"" + cursor + "\"";

        const std::string body =
            "{\"query\":\"query NewSearchTeachersQuery(\\n  $query: TeacherSearchQuery!\\n) {\\n  newSearch {\\n    teachers(query: $query) {\\n      edges {\\n        cursor\\n        node {\\n          id\\n          firstName\\n          lastName\\n          avgRating\\n          avgDifficulty\\n          numRatings\\n          wouldTakeAgainPercent\\n          department\\n        }\\n      }\\n      pageInfo {\\n        hasNextPage\\n        endCursor\\n      }\\n    }\\n  }\\n}\","
            "\"variables\":{\"query\":{\"text\":\"\",\"schoolID\":\"" + schoolId + "\",\"fallback\":true" + afterClause + "}}}";

        std::string response = postGraphQL(body);

        try {
            auto j = json::parse(response);
            auto& teachers = j["data"]["newSearch"]["teachers"];
            auto& edges = teachers["edges"];
            auto& pageInfo = teachers["pageInfo"];

            for (auto& edge : edges) {
                auto& node = edge["node"];
                Professor p;
                p.firstName     = node.value("firstName", "");
                p.lastName      = node.value("lastName", "");
                p.department    = node.value("department", "Unknown");
                p.avgRating     = node.value("avgRating", 0.0f);
                p.avgDifficulty = node.value("avgDifficulty", 0.0f);
                p.numRatings    = node.value("numRatings", 0);
                p.wouldTakeAgain = node.value("wouldTakeAgainPercent", -1.0f);

                if (p.numRatings > 0)
                    professors.push_back(p);
            }

            hasNextPage = pageInfo.value("hasNextPage", false);
            cursor = pageInfo.value("endCursor", "");

        } catch (const std::exception& e) {
            std::cerr << "❌ Fetch error: " << e.what() << "\n";
            break;
        }
    }

    return professors;
}

// ─── Case-insensitive string contains ────────────────────────────────────────
static bool containsCI(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    std::string h = haystack, n = needle;
    std::transform(h.begin(), h.end(), h.begin(), ::tolower);
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    return h.find(n) != std::string::npos;
}

// ─── Display leaderboard ──────────────────────────────────────────────────────
void displayLeaderboard(std::vector<Professor> professors, const std::string& deptFilter) {

    // Filter by department if specified
    if (!deptFilter.empty()) {
        professors.erase(
            std::remove_if(professors.begin(), professors.end(),
                [&](const Professor& p) {
                    return !containsCI(p.department, deptFilter);
                }),
            professors.end()
        );
    }

    if (professors.empty()) {
        std::cout << "❌ No professors found";
        if (!deptFilter.empty()) std::cout << " in department: " << deptFilter;
        std::cout << "\n";
        return;
    }

    // Sort by avgRating descending, then by numRatings descending as tiebreaker
    std::sort(professors.begin(), professors.end(), [](const Professor& a, const Professor& b) {
        if (a.avgRating != b.avgRating) return a.avgRating > b.avgRating;
        return a.numRatings > b.numRatings;
    });

    // ── Header ────────────────────────────────────────────────────────────────
    std::cout << "\n";
    std::cout << "🏆  Professor Leaderboard";
    if (!deptFilter.empty()) std::cout << " — " << deptFilter;
    std::cout << "\n";
    std::cout << std::string(80, '-') << "\n";

    std::cout << std::left
              << std::setw(4)  << "#"
              << std::setw(25) << "Name"
              << std::setw(22) << "Department"
              << std::setw(8)  << "Rating"
              << std::setw(8)  << "Diff"
              << std::setw(8)  << "Again%"
              << std::setw(8)  << "Reviews"
              << "\n";

    std::cout << std::string(80, '-') << "\n";

    // ── Rows (top 20) ─────────────────────────────────────────────────────────
    int limit = std::min((int)professors.size(), 20);
    for (int i = 0; i < limit; i++) {
        const Professor& p = professors[i];

        // Truncate long names/departments
        std::string name = p.firstName + " " + p.lastName;
        if (name.size() > 23) name = name.substr(0, 20) + "...";

        std::string dept = p.department;
        if (dept.size() > 20) dept = dept.substr(0, 17) + "...";

        // Medal for top 3
        std::string rank;
        if      (i == 0) rank = "🥇";
        else if (i == 1) rank = "🥈";
        else if (i == 2) rank = "🥉";
        else             rank = std::to_string(i + 1) + " ";

        // Format wouldTakeAgain
        std::string wta;
        if (p.wouldTakeAgain < 0) wta = "N/A";
        else {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(0) << p.wouldTakeAgain << "%";
            wta = oss.str();
        }

        std::cout << std::left
                  << std::setw(4)  << rank
                  << std::setw(25) << name
                  << std::setw(22) << dept
                  << std::setw(8)  << std::fixed << std::setprecision(1) << p.avgRating
                  << std::setw(8)  << std::fixed << std::setprecision(1) << p.avgDifficulty
                  << std::setw(8)  << wta
                  << std::setw(8)  << p.numRatings
                  << "\n";
    }

    std::cout << std::string(80, '-') << "\n";
    std::cout << "Showing top " << limit << " of " << professors.size() << " professors\n\n";
}


int main(){
    std::cout << "Welcome to Professor Leaderboard!" << std::endl;

    std::string schoolName = "Bloomsburg University";

    // std::cout << "Enter university name: ";
    // std::getline(std::cin, schoolName);


    // std::cout << "\nSearching RateMyProfessors for: " << schoolName << std::endl << std::endl;

    std::string schoolJson = fetchSchoolId(schoolName);
    std::string schoolId = parseSchoolId(schoolJson);
    std::vector<Professor> professors = fetchAndParseProfessors(schoolId);

    std::cout << professors.size() << std::endl;

    std::string deptFilter = "Mathematical & Digital Sciences";
    displayLeaderboard(professors, deptFilter);

    return 0;
}