#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <cstdlib>
#include <iomanip>
#include <algorithm>
#include <omp.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

struct InfoCliente {
    double monto_total = 0.0;
    long cantidad_transacciones = 0;
};

struct Metricas {
    double suma_fem = 0.0, suma_masc = 0.0;
    long cuenta_fem = 0, cuenta_masc = 0;
};

static size_t WriteStringCallback(void* ptr, size_t size, size_t nmemb, std::string* stream) {
    size_t total = size * nmemb;
    stream->append((char*)ptr, total);
    return total;
}

static size_t WriteFileCallback(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    return fwrite(ptr, size, nmemb, stream);
}

class Logger {
private:
    static inline std::mutex log_mutex;
public:
    static void log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::ofstream f("log.txt", std::ios::app);
        if (f.is_open()) f << "[ERROR] " << msg << "\n";
    }
};

class AuthManager {
private:
    std::string jwt_token;
    std::shared_mutex rw_mutex;
    const std::string AUTH_URL = "https://api.sebastian.cl/cpyd/v1/login/authenticate";

    std::string fetchTokenFromAPI() {
        const char* env_email = std::getenv("API_EMAIL");
        const char* env_rut = std::getenv("API_RUT");
        if (!env_email || !env_rut) return "";

        CURL* curl = curl_easy_init();
        if (!curl) return "";

        std::string readBuffer;
        std::string body_json = json{{"email", env_email}, {"rut", env_rut}}.dump();
        struct curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
        
        curl_easy_setopt(curl, CURLOPT_URL, AUTH_URL.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_json.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        std::string new_token = "";
        if (curl_easy_perform(curl) == CURLE_OK) {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code == 200 || http_code == 201) {
                json response = json::parse(readBuffer, nullptr, false);
                if (!response.is_discarded() && response.contains("jwt")) {
                    new_token = response["jwt"].get<std::string>();
                }
            }
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return new_token;
    }

public:
    bool authenticate() {
        std::string token = fetchTokenFromAPI();
        if (token.empty()) return false;
        std::unique_lock<std::shared_mutex> lock(rw_mutex);
        jwt_token = token;
        return true;
    }

    std::string getToken() {
        std::shared_lock<std::shared_mutex> lock(rw_mutex);
        return jwt_token;
    }

    void refreshToken(const std::string& old_token) {
        std::unique_lock<std::shared_mutex> lock(rw_mutex);
        if (jwt_token == old_token) {
            std::string new_t = fetchTokenFromAPI();
            if(!new_t.empty()) jwt_token = new_t;
        }
    }
};

class SFTPManager {
public:
    static std::vector<std::string> getMissingFiles(const std::string& local_path) {
        std::vector<std::string> missing;
        CURL* curl_sftp = curl_easy_init();
        if (!curl_sftp) return missing;

        std::string listado_crudo = "";
        std::string url_raiz = "sftp://" + std::string(std::getenv("SFTP_HOST")) + "/";
        std::string creds = std::string(std::getenv("SFTP_USER")) + ":" + std::string(std::getenv("SFTP_PASS"));

        curl_easy_setopt(curl_sftp, CURLOPT_URL, url_raiz.c_str());
        curl_easy_setopt(curl_sftp, CURLOPT_SSH_AUTH_TYPES, CURLSSH_AUTH_PASSWORD);
        curl_easy_setopt(curl_sftp, CURLOPT_USERPWD, creds.c_str());
        curl_easy_setopt(curl_sftp, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl_sftp, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl_sftp, CURLOPT_DIRLISTONLY, 1L);
        curl_easy_setopt(curl_sftp, CURLOPT_WRITEFUNCTION, WriteStringCallback);
        curl_easy_setopt(curl_sftp, CURLOPT_WRITEDATA, &listado_crudo);
        curl_easy_setopt(curl_sftp, CURLOPT_TIMEOUT, 20L);
        curl_easy_setopt(curl_sftp, CURLOPT_NOSIGNAL, 1L);

        if (curl_easy_perform(curl_sftp) == CURLE_OK) {
            std::stringstream ss(listado_crudo);
            std::string linea;
            while (std::getline(ss, linea)) {
                size_t p_ini = linea.find("reporte_");
                size_t p_fin = linea.find(".csv");
                if (p_ini != std::string::npos && p_fin != std::string::npos) {
                    std::string filename = linea.substr(p_ini, (p_fin + 4) - p_ini);
                    fs::path full_path = fs::path(local_path) / filename;
                    std::error_code ec;
                    if (!fs::exists(full_path) || fs::file_size(full_path, ec) < 100) {
                        missing.push_back(filename);
                    }
                }
            }
        }
        curl_easy_cleanup(curl_sftp);
        return missing;
    }

    static void executeDownloads(const std::vector<std::string>& files, const std::string& local_path) {
        if (files.empty()) return;
        omp_set_num_threads(std::min(4, (int)files.size()));

        #pragma omp parallel
        {
            CURL* curl = curl_easy_init();
            if (curl) {
                std::string creds = std::string(std::getenv("SFTP_USER")) + ":" + std::string(std::getenv("SFTP_PASS"));
                curl_easy_setopt(curl, CURLOPT_SSH_AUTH_TYPES, CURLSSH_AUTH_PASSWORD);
                curl_easy_setopt(curl, CURLOPT_USERPWD, creds.c_str());
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
                curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

                #pragma omp for schedule(dynamic)
                for (int i = 0; i < (int)files.size(); ++i) {
                    std::string url = "sftp://" + std::string(std::getenv("SFTP_HOST")) + "/" + files[i];
                    std::string filepath = local_path + "/" + files[i];
                    FILE* fp = fopen(filepath.c_str(), "wb");
                    if (fp) {
                        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
                        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
                        curl_easy_perform(curl);
                        fclose(fp);
                    }
                }
                curl_easy_cleanup(curl);
            }
        }
    }
};

class DataProcessor {
public:
    static std::unordered_map<std::string, InfoCliente> parseAll(const std::string& local_path) {
        std::error_code ec;
        std::vector<std::string> all_files;
        if (!fs::exists(local_path, ec)) return {}; 
        
        for (const auto& entry : fs::directory_iterator(local_path, ec)) {
            if (!ec && entry.is_regular_file() && entry.path().extension() == ".csv") {
                all_files.push_back(entry.path().string());
            }
        }

        std::unordered_map<std::string, InfoCliente> global_map;
        if (all_files.empty()) return global_map;

        omp_set_num_threads(std::min((int)omp_get_max_threads(), (int)all_files.size()));

        #pragma omp parallel
        {
            std::unordered_map<std::string, InfoCliente> local_map;
            auto clean_sv = [](std::string_view sv) {
                while (!sv.empty() && (sv.front() == '"' || sv.front() == ' ' || sv.front() == '\t')) sv.remove_prefix(1);
                while (!sv.empty() && (sv.back() == '\r' || sv.back() == '\n' || sv.back() == '"' || sv.back() == ' ' || sv.back() == '\t')) sv.remove_suffix(1);
                return sv;
            };

            #pragma omp for schedule(dynamic)
            for (int i = 0; i < (int)all_files.size(); ++i) {
                std::error_code ec_file;
                if (fs::file_size(all_files[i], ec_file) < 100) continue;
                std::ifstream file(all_files[i]);
                if (!file.is_open()) continue;
                std::string linea;
                std::getline(file, linea); 

                while (std::getline(file, linea)) {
                    if (linea.length() < 10) continue;
                    size_t pos = 0, next_pos = 0;
                    int col = 1;
                    std::string_view monto_sv, uuid_sv;
                    while ((next_pos = linea.find(';', pos)) != std::string::npos) {
                        if (col == 7) monto_sv = std::string_view(linea.data() + pos, next_pos - pos);
                        pos = next_pos + 1;
                        col++;
                        if (col == 10) break;
                    }
                    if (col == 10) {
                        next_pos = linea.find(';', pos);
                        uuid_sv = (next_pos == std::string::npos) ? std::string_view(linea.data() + pos) : std::string_view(linea.data() + pos, next_pos - pos);
                    }
                    monto_sv = clean_sv(monto_sv);
                    uuid_sv = clean_sv(uuid_sv);
                    if (!monto_sv.empty() && uuid_sv.length() == 36) {
                        try {
                            local_map[std::string(uuid_sv)].monto_total += std::stod(std::string(monto_sv));
                            local_map[std::string(uuid_sv)].cantidad_transacciones++;
                        } catch (...) {}
                    }
                }
            }
            #pragma omp critical(merge_maps)
            {
                for (const auto& [uuid, info] : local_map) {
                    global_map[uuid].monto_total += info.monto_total;
                    global_map[uuid].cantidad_transacciones += info.cantidad_transacciones;
                }
            }
        }
        return global_map;
    }

    static Metricas queryAPIAll(const std::unordered_map<std::string, InfoCliente>& global_map, AuthManager& auth) {
        std::vector<std::pair<std::string, InfoCliente>> all_clients(global_map.begin(), global_map.end());
        Metricas global_metrics;
        int total_clientes = all_clients.size();
        int procesados_global = 0;

        // MÁXIMA POTENCIA: 600 HILOS
        int num_threads = std::min(600, total_clientes);
        omp_set_num_threads(num_threads);

        #pragma omp parallel
        {
            CURL* curl = curl_easy_init();
            Metricas local_metrics;
            int local_procesados = 0;

            if (curl) {
                // OPTIMIZACIONES ABSOLUTAS DE RED
                curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
                curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
                curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);
                curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // Acepta GZIP (compresión)
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // Evita retrasos de handshake
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
                curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 600L); // Caché de DNS

                // CHUNKING: Bloques de 50 para balancear carga
                #pragma omp for schedule(dynamic, 50)
                for (int i = 0; i < total_clientes; ++i) {
                    const auto& [uuid, info] = all_clients[i];
                    std::string url = "https://api.sebastian.cl/cpyd/v1/person/" + uuid;
                    std::string readBuffer;
                    std::string genero = "NO_DEFINIDO";

                    int reintentos = 0;
                    while (reintentos < 2) {
                        readBuffer.clear();
                        std::string current_token = auth.getToken();
                        
                        struct curl_slist* headers = nullptr;
                        std::string auth_header = "Authorization: Bearer " + current_token;
                        headers = curl_slist_append(headers, auth_header.c_str());
                        headers = curl_slist_append(headers, "Connection: keep-alive");
                        
                        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
                        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
                        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);
                        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

                        CURLcode res = curl_easy_perform(curl);
                        long http_code = 0;
                        if (res == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                        curl_slist_free_all(headers);

                        if (http_code == 200) {
                            json j = json::parse(readBuffer, nullptr, false);
                            if (!j.is_discarded() && j.contains("gender") && j["gender"].is_string()) {
                                genero = j["gender"].get<std::string>();
                                std::transform(genero.begin(), genero.end(), genero.begin(), ::toupper);
                                if (genero == "FEMALE" || genero == "F") genero = "FEMENINO";
                                if (genero == "MALE" || genero == "M") genero = "MASCULINO";
                            }
                            break;
                        } else if (http_code == 401 || http_code == 403) {
                            auth.refreshToken(current_token);
                            reintentos++;
                        } else {
                            break;
                        }
                    }

                    if (genero == "FEMENINO") {
                        local_metrics.suma_fem += info.monto_total;
                        local_metrics.cuenta_fem += info.cantidad_transacciones;
                    } else if (genero == "MASCULINO") {
                        local_metrics.suma_masc += info.monto_total;
                        local_metrics.cuenta_masc += info.cantidad_transacciones;
                    }

                    local_procesados++;
                    // REDUCCIÓN DE LOCKS: Imprimir cada 500 para no ahogar la terminal
                    if (local_procesados % 500 == 0) {
                        #pragma omp critical(progreso)
                        {
                            procesados_global += local_procesados;
                            local_procesados = 0;
                            std::cout << "[Progreso] Procesados " << procesados_global << " / " << total_clientes << " clientes...\r" << std::flush;
                        }
                    }
                }
                curl_easy_cleanup(curl);
            }
            #pragma omp critical(acumulado_final)
            {
                global_metrics.suma_fem += local_metrics.suma_fem;
                global_metrics.cuenta_fem += local_metrics.cuenta_fem;
                global_metrics.suma_masc += local_metrics.suma_masc;
                global_metrics.cuenta_masc += local_metrics.cuenta_masc;
                procesados_global += local_procesados;
            }
        }
        std::cout << "\n";
        return global_metrics;
    }
};

int main() {
    double inicio_reloj = omp_get_wtime();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::ofstream("log.txt", std::ios::trunc).close();

    if (!std::getenv("API_EMAIL") || !std::getenv("API_RUT") || !std::getenv("SFTP_USER") || !std::getenv("SFTP_PASS") || !std::getenv("SFTP_HOST")) {
        std::cerr << "Faltan credenciales en variables de entorno. Abortando.\n";
        return EXIT_FAILURE;
    }

    std::string local_dir = "data";
    std::error_code ec;
    if (!std::filesystem::exists(local_dir, ec)) {
        std::filesystem::create_directories(local_dir, ec);
    }

    AuthManager auth;
    if (!auth.authenticate()) {
        std::cerr << "Fallo en la autenticación inicial. Revisa log.txt.\n";
        return EXIT_FAILURE;
    }
    std::cout << "[INFO] Autenticación Exitosa.\n\n[INFO] Sincronizando con SFTP...\n";
    
    std::vector<std::string> missing_files = SFTPManager::getMissingFiles(local_dir);
    if (!missing_files.empty()) {
        SFTPManager::executeDownloads(missing_files, local_dir);
    } else {
        std::cout << "-> Todo está sincronizado en local.\n";
    }

    std::cout << "\n[INFO] Parseando CSVs en local...\n";
    auto clientes_unicos = DataProcessor::parseAll(local_dir);
    if (clientes_unicos.empty()) return 0;

    std::cout << "\n[INFO] EXTREMO: Consultando API con 600 Hilos OpenMP para " << clientes_unicos.size() << " clientes...\n";
    Metricas metricas = DataProcessor::queryAPIAll(clientes_unicos, auth);

    double fin_reloj = omp_get_wtime();
    double duracion = fin_reloj - inicio_reloj;
    curl_global_cleanup();

    std::stringstream formato_final;
    if (metricas.cuenta_fem > 0) formato_final << "FEMENINO = " << std::fixed << std::setprecision(2) << (metricas.suma_fem / metricas.cuenta_fem) << "\n";
    if (metricas.cuenta_masc > 0) formato_final << "MASCULINO = " << std::fixed << std::setprecision(2) << (metricas.suma_masc / metricas.cuenta_masc) << "\n";
    formato_final << "TIEMPO = " << duracion << " segundos\n";

    std::cout << "\n================ RESULTADOS ================\n" << formato_final.str();
    std::cout << "============================================\n";

    std::ofstream archivo_salida("resultados.txt");
    if (archivo_salida.is_open()) archivo_salida << formato_final.str();

    return EXIT_SUCCESS;
}
