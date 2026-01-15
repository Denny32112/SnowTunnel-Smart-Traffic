#include "crow_all.h"
#include "json.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>
#include <cstdio> // 包含 popen/pclose

using json = nlohmann::json;
using namespace std;
using namespace std::chrono;

// 🔴 這裡填入你的 GCP 上的金鑰
string CLIENT_ID = "你的真實Client_ID";     
string CLIENT_SECRET = "你的真實Client_Secret"; 

json CACHED_DATA;           
auto LAST_FETCH_TIME = steady_clock::now(); 
bool IS_FIRST_RUN = true;   

// Linux 版本 exec (無底線)
string exec(const char* cmd) {
    char buffer[128];
    string result = "";
    FILE* pipe = popen(cmd, "r"); // Linux use popen
    if (!pipe) return "ERROR";
    try {
        while (fgets(buffer, sizeof buffer, pipe) != NULL) result += buffer;
    } catch (...) { pclose(pipe); return "ERROR"; }
    pclose(pipe);
    return result;
}

string getAccessToken() {
    ofstream authFile("auth_body.txt");
    if (!authFile.is_open()) return "";
    authFile << "grant_type=client_credentials&client_id=" << CLIENT_ID << "&client_secret=" << CLIENT_SECRET;
    authFile.close();
    string url = "https://tdx.transportdata.tw/auth/realms/TDXConnect/protocol/openid-connect/token";
    string cmd = "curl -k -s -X POST -d @auth_body.txt \"" + url + "\"";
    string response = exec(cmd.c_str());
    remove("auth_body.txt");
    try {
        auto jsonResponse = json::parse(response);
        if (jsonResponse.contains("access_token")) return jsonResponse["access_token"];
    } catch (...) {}
    return "";
}

bool downloadData(string url, string output_filename, string token) {
    string header = " -H \"Authorization: Bearer " + token + "\"";
    header += " -H \"Accept: application/json\"";
    string command = "curl -k -L -s" + header + " -o " + output_filename + " \"" + url + "\"";
    int result = system(command.c_str());
    return (result == 0);
}

// 統計結構
struct LaneStats {
    double total_speed_inner = 0;
    int count_inner = 0;
    double total_speed_outer = 0;
    int count_outer = 0;
};

json getAverageTrafficData() {
    auto now = steady_clock::now();
    auto elapsed = duration_cast<seconds>(now - LAST_FETCH_TIME).count();

    if (!IS_FIRST_RUN && elapsed < 60) {
        return CACHED_DATA;
    }

    cout << "[Update] Linux Server Updating..." << endl;
    string token = getAccessToken();
    if (token == "") return {{"error", "Auth failed"}};

    string url = "https://tdx.transportdata.tw/api/basic/v2/Road/Traffic/Live/VD/Freeway";
    string filename = "traffic_data.json";

    if (!downloadData(url, filename, token)) return {{"error", "Download failed"}};

    ifstream f(filename);
    if (!f.is_open()) return {{"error", "File open failed"}};

    try {
        json data = json::parse(f);
        json* target_list = nullptr;
        if (data.contains("VDLives")) target_list = &data["VDLives"];
        else if (data.contains("VDList")) target_list = &data["VDList"];
        else if (data.is_array()) target_list = &data;

        LaneStats stats_north;
        LaneStats stats_south;

        if (target_list) {
            for (auto& vd : *target_list) {
                string vdid = vd.value("VDID", "Unknown");
                
                if (vdid.find("N5") != string::npos && vdid.find("-M-") != string::npos) {
                    double km = 0;
                    try { 
                        size_t dash1 = vdid.find('-');
                        size_t dash2 = vdid.find('-', dash1 + 1);
                        size_t dash3 = vdid.find('-', dash2 + 1);
                        km = stod(vdid.substr(dash3 + 1)); 
                    } catch(...) { continue; }

                    if (km >= 15.0 && km <= 28.0) {
                        LaneStats* target = nullptr;
                        if (vdid.find("N5-N") != string::npos) target = &stats_north;
                        else if (vdid.find("N5-S") != string::npos) target = &stats_south;
                        else continue;

                        if (vd.contains("LinkFlows")) {
                            for (auto& link : vd["LinkFlows"]) {
                                if (link.contains("Lanes")) {
                                    for (auto& lane : link["Lanes"]) {
                                        int lid = lane.value("LaneID", -999);
                                        double spd = lane.value("Speed", 0.0);
                                        if (spd > 0) {
                                            if (lid == 0) {
                                                target->total_speed_inner += spd;
                                                target->count_inner++;
                                            }
                                            if (lid == 1) {
                                                target->total_speed_outer += spd;
                                                target->count_outer++;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        json result;
        // 北上
        if (stats_north.count_inner > 0) {
            int avg_in = stats_north.total_speed_inner / stats_north.count_inner;
            int avg_out = stats_north.total_speed_outer / stats_north.count_outer;
            int diff = avg_out - avg_in;
            string advice = "均可";
            if (diff >= 3) advice = "靠右 (外側快)";
            else if (diff <= -3) advice = "靠左 (內側快)";

            result["north"] = {
                {"inner", avg_in}, {"outer", avg_out},
                {"advice", advice}, {"diff", abs(diff)}
            };
        } else { result["north"] = {{"error", "無數據"}}; }

        // 南下
        if (stats_south.count_inner > 0) {
            int avg_in = stats_south.total_speed_inner / stats_south.count_inner;
            int avg_out = stats_south.total_speed_outer / stats_south.count_outer;
            int diff = avg_out - avg_in;
            string advice = "均可";
            if (diff >= 3) advice = "靠右 (外側快)";
            else if (diff <= -3) advice = "靠左 (內側快)";

            result["south"] = {
                {"inner", avg_in}, {"outer", avg_out},
                {"advice", advice}, {"diff", abs(diff)}
            };
        } else { result["south"] = {{"error", "無數據"}}; }
        
        CACHED_DATA = result;
        LAST_FETCH_TIME = now;
        IS_FIRST_RUN = false;
        return CACHED_DATA;
    } catch (...) { return {{"error", "JSON parse error"}}; }
}

int main() {
    crow::SimpleApp app;

    CROW_ROUTE(app, "/api/average")([](){
        json data = getAverageTrafficData();
        return crow::response(data.dump());
    });

    CROW_ROUTE(app, "/")([](){
        ifstream in("index.html");
        string str((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
        return str;
    });

    // Linux 上綁定 0.0.0.0 才能被外網訪問
    // 使用 Port 8080 (記得防火牆要開)
    app.port(8080).bindaddr("0.0.0.0").multithreaded().run();
}