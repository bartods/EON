
#include <HTTPClient.h>
#include <Update.h>
#include <LittleFS.h>
#include "esp_ota_ops.h" // Nagłówek dla funkcji OTA

using namespace fs;

// URL do pliku CSV i firmware
const char* versionURL = "https://raw.githubusercontent.com/bartods/EON/main/version.csv";
String newFirmwareURL;
String serverVersion;


// Aktualna wersja firmware w urządzeniu
const String currentVersion = "1.12";





bool checkForUpdate() {
    HTTPClient http;
    http.begin(versionURL);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("Błąd HTTP podczas pobierania wersji terminala: %d\n", httpCode);
        String message = "ERROR: %d\n", httpCode;
        lv_textarea_set_text(ui_TextAreaSettings, message.c_str());
        lv_timer_handler();

        delay(2000);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    int commaIndex = payload.indexOf(',');
    
    if (commaIndex != -1) {
        serverVersion = payload.substring(0, commaIndex);
        serverVersion.trim();
        
        String tempFirmwareURL = payload.substring(commaIndex + 1);
        tempFirmwareURL.trim();
        newFirmwareURL = tempFirmwareURL;
        
        Serial.println("Wersja terminala: " + currentVersion);
        Serial.println("Wersja dostępna: " + serverVersion);

        if (serverVersion > currentVersion) {
            Serial.println("Dostępna nowa wersja terminala!");
            String message = "NEW VERSION OF TERMINAL AVALIABLE " + serverVersion + "\nCURRENT VERSION " + currentVersion;
            lv_textarea_set_text(ui_TextAreaSettings, message.c_str());
            lv_timer_handler();

            delay(2000);
            lv_obj_add_flag(ui_settingsYES, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_settingsNO, LV_OBJ_FLAG_HIDDEN);
            lv_timer_handler();
            delay(2000);

            return true;
        } else {
            String message = "VERSION OF SOFTWARE IS ACTUAL " + currentVersion + "\nAVALIABLE VERSION" + serverVersion;
            lv_textarea_set_text(ui_TextAreaSettings, message.c_str());
            lv_timer_handler(); //resetowanie watchdog

            Serial.println("Wersja aktualna. Brak potrzeby aktualizacji terminala.");
            delay(2000);
            lv_obj_add_flag(ui_TextAreaSettings, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_settingsYES, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_settingsNO, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_settingsUpdate, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_settingsVibration, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_switchLED, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_settingsCalibrationPH, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_settingsExit, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_settingsLanguage, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_RollerLanguage, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_settingsBacklight, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_backlightSlider, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_switchVibration, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_settingsLED, LV_OBJ_FLAG_HIDDEN);
            lv_timer_handler();
            return false;
        }
    } else {
        Serial.println("Nie znaleziono przecinka w danych CSV!");
        return false;
    }
}



bool downloadFirmware() {
    HTTPClient http;
    http.begin(newFirmwareURL);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("Błąd HTTP podczas pobierania firmware: %d\n", httpCode);
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    WiFiClient* stream = http.getStreamPtr();
    
    if (LittleFS.remove("/firmware.bin")) {
        Serial.println("Plik firmware.bin został usunięty.");
    } else {
        Serial.println("Nie udało się usunąć pliku firmware.bin.");
    }
    
    File file = LittleFS.open("/firmware.bin", "w");

    if (!file) {
        Serial.println("Nie udało się otworzyć pliku do zapisu na LittleFS!");
        http.end();
        return false;
    }

    Serial.printf("Rozpoczynanie pobierania firmware (%d bajtów)...\n", contentLength);
    
    updating = true;
    
    uint8_t buffer[4096];

    int written = 0;
    size_t lastUpdate = 0;

    while (http.connected() && (written < contentLength)) {
        size_t size = stream->available();
        if (size) {
            int bytesRead = stream->readBytes(buffer, sizeof(buffer));
            file.write(buffer, bytesRead);
            written += bytesRead;

            if (written - lastUpdate >= 1024 || written * 100 / contentLength > lastUpdate * 100 / contentLength) {
                lastUpdate = written;
                String message = String("DOWNLOADING\n ") + String(written) + "/" + String(contentLength) + " bytes";
                Serial.println(message);
                lv_textarea_set_text(ui_TextAreaSettings, message.c_str());
                lv_timer_handler();
            }
        }
        delay(1);
    }

    file.close();
    http.end();

    if (written == contentLength) {
        Serial.println("Firmware zapisany na LittleFS.");
        return true;
    } else {
        Serial.println("Błąd podczas pobierania firmware!");
        return false;
    }
}

bool performUpdate() {
    // Disable watchdog timer during setup
    disableCore0WDT();
    disableCore1WDT();
    // Get update partition
    const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
    if (!updatePartition) {
        Serial.println("No OTA partition found!");
        return false;
    }

    // Open firmware file
    File file = LittleFS.open("/firmware.bin", "r");
    if (!file) {
        Serial.println("Failed to open firmware file!");
        return false;
    }

    size_t fileSize = file.size();
    Serial.printf("Update file size: %u bytes\n", fileSize);
    Serial.printf("Update partition size: %u bytes\n", updatePartition->size);

    // Verify size
    if (fileSize > updatePartition->size) {
        Serial.println("Update file too large!");
        file.close();
        return false;
    }

    // Begin update with specific parameters
    if (!Update.begin(fileSize, U_FLASH, -1, LOW)) {
        Serial.printf("Update.begin failed! Error: %s\n", Update.errorString());
        file.close();
        return false;
    }

//    // Set timeout for write operations
//    Update.setTimeout(60000);  // 60 seconds timeout

    // Use larger buffer for better performance
    const size_t bufferSize = 1024;
    uint8_t *buffer = (uint8_t*)malloc(bufferSize);
    if (!buffer) {
        Serial.println("Failed to allocate buffer!");
        file.close();
        return false;
    }

    size_t written = 0;
    while (written < fileSize) {
        // Read from file
        size_t toRead = min(bufferSize, fileSize - written);
        size_t bytesRead = file.read(buffer, toRead);
        
        if (bytesRead == 0) {
            Serial.println("Read error!");
            free(buffer);
            file.close();
            return false;
        }

        // Write to update partition
        if (Update.write(buffer, bytesRead) != bytesRead) {
            Serial.printf("Update write failed! Error: %s\n", Update.errorString());
            free(buffer);
            file.close();
            return false;
        }
        
        float lastProgress = 0;  // Track the last reported progress percentage
        
        // In the update loop:
        written += bytesRead;
        float currentProgress = (written * 100.0) / fileSize;
        
        // Only update when progress increases by 10% or more
        if (currentProgress - lastProgress >= 10.0 || written == fileSize) {
            String message = String("UPDATING FIRMWARE...\n") + 
                            String(written) + "/" + String(fileSize) + " bytes\n" +
                            String(currentProgress, 1) + "%";
            
            Serial.println(message);
            lv_textarea_set_text(ui_TextAreaSettings, message.c_str());
            lv_timer_handler();
            
            lastProgress = currentProgress;
        }

        // Allow other tasks to run
        delay(1);
    }

    free(buffer);
    file.close();

    if (!Update.end(true)) {
        Serial.printf("Update.end failed! Error: %s\n", Update.errorString());
        return false;
    }

    Serial.println("Update successful! Rebooting...");
    return true;
}


void switchToEspNow() {
    Serial.println("Przełączanie do trybu ESP-NOW...");
    WiFi.disconnect(true);
    WiFi.persistent(false);
    bool ok = WifiEspNowBroadcast.begin("EON", 3);
    if (!ok) {
        Serial.println("WifiEspNowBroadcast.begin() failed");
        ESP.restart();
    }
    WifiEspNowBroadcast.onReceive(processRx, nullptr);
    Serial.print("MAC address of this node is ");
    Serial.println(WiFi.softAPmacAddress());
}

void switchToWiFiClient() {
    Serial.println("Przełączanie do trybu WiFi klienta...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("\nPołączono z Wi-Fi!");
}

void connectWifiAndUpdate() {
    Serial.println("Rozpoczynanie procesu aktualizacji...");
    String message = "CHECKING UPGRADE...\n";
    lv_textarea_set_text(ui_TextAreaSettings, message.c_str());
    lv_timer_handler();

    delay(2000);
    switchToWiFiClient();

    if (!LittleFS.begin(false)) {
        Serial.println("LittleFS mount failed, formatting...");
        if (LittleFS.format()) {
            Serial.println("LittleFS formatted successfully");
            LittleFS.begin();
        } else {
            Serial.println("LittleFS formatting failed");
            switchToEspNow();
            return;
        }
    }

    if (checkForUpdate()) {
        if (downloadFirmware() && performUpdate()) {
            String message = "UPDATE OK\nREBOTING...";
            lv_textarea_set_text(ui_TextAreaSettings, message.c_str());
            lv_timer_handler();
            Serial.println("Aktualizacja zakończona sukcesem!");
            delay(2000);
            ESP.restart();

        } else {
            Serial.println("Aktualizacja nie powiodła się.");
                        String message = "UPDATE FAILED!\nREBOTING...";
            lv_textarea_set_text(ui_TextAreaSettings, message.c_str());
            lv_timer_handler();
            delay(2000);
            ESP.restart();
        }
    } else {
        Serial.println("Brak aktualizacji terminala.");
    }



    switchToEspNow();
}
