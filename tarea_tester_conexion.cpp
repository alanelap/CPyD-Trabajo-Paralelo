#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <cstdlib> 
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <string_view>
#include <chrono>

using json = nlohmann::json;
namespace fs = std::filesystem;

std::string token_dinamico_global = "";

struct InfoCliente {
    double monto_total = 0.0;
    long cantidad_transacciones = 0;
};

size_t WriteStringCallback(void* ptr, size_t size, size_t nmemb, std::string* stream) {
    size_t total = size * nmemb;
    stream->append((char*)ptr, total);
    return total;
}

size_t WriteFileCallback(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    return fwrite(ptr, size, nmemb, stream);
}

void print_status(const std::string& msg) {
    std::cout << "[TESTER] " << msg << std::endl;
}

std::string obtener_token() {
    print_status("Solicitando token JWT a la API...");
    const char* env_email = std::getenv("API_EMAIL");
    const char* env_rut = std::getenv("API_RUT");
    if (!env_email || !env_rut) return ""; 

    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string readBuffer;
    std::string token_extraido = "";
    std::string body_json_str = json{{"email", std::string(env_email)}, {"rut", std::string(env_rut)}}.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, "https://api.sebastian.cl/cpyd/v1/login/authenticate");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_json_str.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    if (curl_easy_perform(curl) == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code == 200 || http_code == 201) {
            json respuesta_json = json::parse(readBuffer, nullptr, false);
            if (!respuesta_json.is_discarded() && respuesta_json.contains("jwt")) {
                token_extraido = respuesta_json["jwt"].get<std::string>();
                print_status("Token JWT obtenido exitosamente.");
                std::cout << "         -> JWT: " << token_extraido << "\n" << std::endl;
            }
        }
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return token_extraido;
}

std::vector<std::string> obtener_lista_archivos_sftp() {
    print_status("Conectando al servidor SFTP para listar un archivo de prueba...");
    std::vector<std::string> archivos;
    CURL* curl_sftp = curl_easy_init();
    std::string listado_crudo = "";

    if (curl_sftp) {
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
        curl_easy_perform(curl_sftp);
        
        std::stringstream ss(listado_crudo);
        std::string linea;
        while (std::getline(ss, linea)) {
            size_t p_ini = linea.find("reporte_");
            size_t p_fin = linea.find(".csv");
            if (p_ini != std::string::npos && p_fin != std::string::npos) {
                archivos.push_back(linea.substr(p_ini, (p_fin + 4) - p_ini));
            }
        }
        curl_easy_cleanup(curl_sftp);
    }
    return archivos;
}

std::string consultar_genero(CURL* curl, const std::string& uuid) {
    std::string url = "https://api.sebastian.cl/cpyd/v1/person/" + uuid;
    std::string readBuffer;
    
    struct curl_slist* headers = nullptr;
    std::string auth_header = "Authorization: Bearer " + token_dinamico_global;
    headers = curl_slist_append(headers, auth_header.c_str());
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    std::string genero = "NO_DEFINIDO";

    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code == 200) {
            json j = json::parse(readBuffer, nullptr, false);
            if (!j.is_discarded() && j.contains("gender") && j["gender"].is_string()) {
                genero = j["gender"].get<std::string>();
            }
        }
    }
    curl_slist_free_all(headers);
    return genero;
}

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    if (!std::getenv("API_EMAIL") || !std::getenv("SFTP_HOST")) {
        std::cerr << "Faltan variables de entorno." << std::endl;
        return 1;
    }

    print_status("Iniciando Modo Tester (Prueba de 1 Sola Operación)");

    token_dinamico_global = obtener_token();
    if (token_dinamico_global.empty()) return 1;

    std::vector<std::string> lista_archivos = obtener_lista_archivos_sftp();
    std::string carpetaDestino = "data_test";
    if (!fs::exists(carpetaDestino)) fs::create_directory(carpetaDestino);

    std::unordered_map<std::string, InfoCliente> clientes_mapa;

    CURL* hilo_sftp = curl_easy_init();
    if (hilo_sftp) {
        for (const std::string& archivo_prueba : lista_archivos) {
            std::string rutaCompleta = carpetaDestino + "/" + archivo_prueba;
            std::string url_descarga = "sftp://" + std::string(std::getenv("SFTP_HOST")) + "/" + archivo_prueba;
            std::string creds = std::string(std::getenv("SFTP_USER")) + ":" + std::string(std::getenv("SFTP_PASS"));
            
            curl_easy_setopt(hilo_sftp, CURLOPT_URL, url_descarga.c_str());
            curl_easy_setopt(hilo_sftp, CURLOPT_SSH_AUTH_TYPES, CURLSSH_AUTH_PASSWORD);
            curl_easy_setopt(hilo_sftp, CURLOPT_USERPWD, creds.c_str());
            curl_easy_setopt(hilo_sftp, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(hilo_sftp, CURLOPT_SSL_VERIFYHOST, 0L);
            
            FILE* fp = fopen(rutaCompleta.c_str(), "wb");
            if (fp) {
                curl_easy_setopt(hilo_sftp, CURLOPT_WRITEFUNCTION, WriteFileCallback);
                curl_easy_setopt(hilo_sftp, CURLOPT_WRITEDATA, fp);
                if (curl_easy_perform(hilo_sftp) == CURLE_OK) {
                    fclose(fp); 
                    
                    std::error_code ec;
                    if (fs::file_size(rutaCompleta, ec) < 100) continue; 

                    std::ifstream archivo_local(rutaCompleta);
                    if (archivo_local.is_open()) {
                        std::string linea_csv;
                        std::getline(archivo_local, linea_csv); 
                        while (std::getline(archivo_local, linea_csv)) {
                            size_t pos = 0, next_pos = 0;
                            int col = 1;
                            std::string_view uuid_sv;
                            while ((next_pos = linea_csv.find(';', pos)) != std::string::npos) {
                                pos = next_pos + 1;
                                col++;
                                if (col == 10) break;
                            }
                            if (col == 10) {
                                next_pos = linea_csv.find(';', pos);
                                uuid_sv = (next_pos == std::string::npos) ? std::string_view(linea_csv.data() + pos) : std::string_view(linea_csv.data() + pos, next_pos - pos);
                                
                                // Limpieza simple
                                while (!uuid_sv.empty() && (uuid_sv.front() == '"' || uuid_sv.front() == ' ')) uuid_sv.remove_prefix(1);
                                while (!uuid_sv.empty() && (uuid_sv.back() == '\r' || uuid_sv.back() == '\n' || uuid_sv.back() == '"' || uuid_sv.back() == ' ')) uuid_sv.remove_suffix(1);
                                
                                if (uuid_sv.length() == 36) {
                                    clientes_mapa[std::string(uuid_sv)].cantidad_transacciones = 1;
                                    break; // ¡SOLO NECESITAMOS 1 CLIENTE PARA LA PRUEBA!
                                }
                            }
                        }
                    }
                } else {
                    fclose(fp);
                }
            }
            if (!clientes_mapa.empty()) break; 
        }
        curl_easy_cleanup(hilo_sftp);
    }

    if (!clientes_mapa.empty()) {
        std::string uuid_prueba = clientes_mapa.begin()->first;
        print_status("Iniciando prueba de rendimiento de UNA SOLA petición API...");
        print_status("UUID a consultar: " + uuid_prueba);

        CURL* hilo_api = curl_easy_init();
        if (hilo_api) {
            // INICIO DEL CRONÓMETRO DE ALTA PRECISIÓN PARA 1 SOLA OPERACIÓN
            auto start_api = std::chrono::high_resolution_clock::now();
            
            std::string genero = consultar_genero(hilo_api, uuid_prueba); 
            
            // FIN DEL CRONÓMETRO
            auto end_api = std::chrono::high_resolution_clock::now();
            
            std::chrono::duration<double, std::milli> ms_double = end_api - start_api;

            std::cout << "\n=====================================================\n";
            std::cout << "Género obtenido exitosamente : " << genero << "\n";
            std::cout << "Tiempo de UNA operación API  : " << ms_double.count() << " milisegundos\n";
            std::cout << "Tiempo en segundos           : " << (ms_double.count() / 1000.0) << " segundos\n";
            std::cout << "=======================================================\n";
            
            curl_easy_cleanup(hilo_api);
        }
    }

    curl_global_cleanup();
    return 0;
}