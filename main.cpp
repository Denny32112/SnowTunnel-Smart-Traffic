#include <iostream>
#include <fstream>
#include <cstdlib>
#include <string>
#include <vector>
#include <cstdio>
#include "json.hpp" 

using json = nlohmann::json;
using namespace std;

// ==========================================
// 🔴 請再次確認 ID 和 Secret 已經填入
// ==========================================
string CLIENT_ID = "請填入你的_ID";     
string CLIENT_SECRET = "請填入你的_SECRET"; 



string exec(const char* cmd) {
    char buffer[128];
    string result = "";
    FILE* pipe = _popen(cmd, "r");
    if (!pipe) return "ERROR";
    try {
        while (fgets(buffer, sizeof buffer, pipe) != NULL) result += buffer;
    } catch (...) { _pclose(pipe); return "ERROR"; }
    _pclose(pipe);
    return result;
}

string getAccessToken() {
    cout << "[Auth] 申請 Token..." << endl;
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

// 用來累積數據的結構
struct LaneStats {
    double total_speed_inner = 0;
    int count_inner = 0;
    double total_speed_outer = 0;
    int count_outer = 0;
};

int main() {
    system("chcp 65001 > nul"); 
    string token = getAccessToken();
    if (token == "") return -1;

    string url = "https://tdx.transportdata.tw/api/basic/v2/Road/Traffic/Live/VD/Freeway";
    string filename = "traffic_data.json";

    cout << "[Info] 正在下載全台即時路況並計算平均值..." << endl;
    if (!downloadData(url, filename, token)) return -1;

    ifstream f(filename);
    try {
        json data = json::parse(f);
        json* target_list = nullptr;
        if (data.contains("VDLives")) target_list = &data["VDLives"];
        else if (data.contains("VDList")) target_list = &data["VDList"];
        else if (data.is_array()) target_list = &data;

        if (target_list) {
            LaneStats stats_north; // 北上 (回台北)
            LaneStats stats_south; // 南下 (去宜蘭)

            for (auto& vd : *target_list) {
                string vdid = vd.value("VDID", "Unknown");
                
                // 篩選 N5 (國道5號) 且 M (主線)
                if (vdid.find("N5") != string::npos && vdid.find("-M-") != string::npos) {
                    
                    double km = 0;
                    try { 
                        size_t dash1 = vdid.find('-');
                        size_t dash2 = vdid.find('-', dash1 + 1);
                        size_t dash3 = vdid.find('-', dash2 + 1);
                        km = stod(vdid.substr(dash3 + 1)); 
                    } catch(...) { continue; }

                    // 只計算雪隧範圍 (15K ~ 28K)
                    if (km >= 15.0 && km <= 28.0) {
                        
                        // 判斷這支偵測器是北上還是南下
                        LaneStats* current_stats = nullptr;
                        if (vdid.find("N5-N") != string::npos) current_stats = &stats_north;
                        else if (vdid.find("N5-S") != string::npos) current_stats = &stats_south;
                        else continue;

                        if (vd.contains("LinkFlows")) {
                            for (auto& link : vd["LinkFlows"]) {
                                if (link.contains("Lanes")) {
                                    for (auto& lane : link["Lanes"]) {
                                        int lid = lane.value("LaneID", -999);
                                        double spd = lane.value("Speed", 0.0);
                                        
                                        // 0=內側, 1=外側 (如果速度是0可能代表沒車或儀器壞了，這裡簡單起見先包含)
                                        if (spd > 0) { // 只算有速度的
                                            if (lid == 0) {
                                                current_stats->total_speed_inner += spd;
                                                current_stats->count_inner++;
                                            }
                                            if (lid == 1) {
                                                current_stats->total_speed_outer += spd;
                                                current_stats->count_outer++;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            cout << "\n========== 雪山隧道 (15K-28K) 平均車速決策板 ==========" << endl;
            
            // --- 顯示北上結果 ---
            cout << "\n【 北上 (宜蘭 -> 台北) 】" << endl;
            if (stats_north.count_inner > 0 && stats_north.count_outer > 0) {
                int avg_in = stats_north.total_speed_inner / stats_north.count_inner;
                int avg_out = stats_north.total_speed_outer / stats_north.count_outer;
                
                cout << "  內側平均: " << avg_in << " km/h" << endl;
                cout << "  外側平均: " << avg_out << " km/h" << endl;
                
                cout << "👉 建議: ";
                if (avg_out - avg_in >= 3) cout << "靠右 (外側快 " << avg_out - avg_in << " km/h) 🚀";
                else if (avg_in - avg_out >= 3) cout << "靠左 (內側快 " << avg_in - avg_out << " km/h) 🚀";
                else cout << "兩邊差不多，隨意選 🚗";
                cout << endl;
            } else {
                cout << "  (目前無足夠數據)" << endl;
            }

            // --- 顯示南下結果 ---
            cout << "\n【 南下 (台北 -> 宜蘭) 】" << endl;
            if (stats_south.count_inner > 0 && stats_south.count_outer > 0) {
                int avg_in = stats_south.total_speed_inner / stats_south.count_inner;
                int avg_out = stats_south.total_speed_outer / stats_south.count_outer;
                
                cout << "  內側平均: " << avg_in << " km/h" << endl;
                cout << "  外側平均: " << avg_out << " km/h" << endl;
                
                cout << "👉 建議: ";
                if (avg_out - avg_in >= 3) cout << "靠右 (外側快 " << avg_out - avg_in << " km/h) 🚀";
                else if (avg_in - avg_out >= 3) cout << "靠左 (內側快 " << avg_in - avg_out << " km/h) 🚀";
                else cout << "兩邊差不多，隨意選 🚗";
                cout << endl;
            } else {
                cout << "  (目前無足夠數據)" << endl;
            }
            cout << "\n=======================================================" << endl;

        }
    } catch (exception& e) {
        cerr << "[Error] " << e.what() << endl;
    }
    return 0;
}