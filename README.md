# Licznik YouTube (yt-counter)

Urządzenie na bazie **ESP8266 (NodeMCU / D1 Mini Pro)** wyświetla na matrycy LED liczbę **subskrypcji, filmów i wyświetleń** wybranego kanału YouTube, pobierając dane z API YouTube.

---

## 1. Co będzie potrzebne

| Element | Uwagi |
|---|---|
| Płytka ESP8266 (np. D1 Mini Pro) | urządzenie robocze |
| Matryca LED MAX72XX (4 moduły FC16) | wyświetlacz 8x32 |
| Zasilacz 5V | płytka + matryca |
| Zasilanie i kable połączeniowe | DATA → D7 (GPIO13), CLK → D5 (GPIO14), CS → D8 (GPIO15) |
| Sieć WiFi 2.4 GHz | urządzenie **nie obsługuje WiFi 5 GHz** |
| Klucz API YouTube (opcjonalnie przy zmianie kanału) | patrz sekcja 4 |

> **Ważne:** Hasła od WiFi i dane API są zapisywane w pamięci urządzenia (LittleFS) i pozostają tam nawet po odłączeniu zasilania.

---

## 2. Pierwsze uruchomienie – konfiguracja WiFi

Po włożeniu zasilania urządzenie sprawdza, czy pamięta zapisane sieci WiFi:

1. **Jeśli pamięta WiFi** – automatycznie się połączy i zacznie wyświetlać statystyki. Możesz pominąć dalszą część instrukcji.
2. **Jeśli nie ma zapisanej sieci** (pierwsze uruchomienie) – urządzenie utworzy **własny punkt dostępowy** (hotspot) i uruchomi portal konfiguracyjny.

### 2.1 Połączenie z punktem dostępowym urządzenia

1. Na telefonie lub komputerze otwórz listę sieci WiFi.
2. Znajdź sieć o nazwie:

   ```
   AutoConnectAP
   ```

3. Połącz się z nią, wpisując hasło:

   ```
   password
   ```

   > Jeśli telefon pokaże ostrzeżenie „brak dostępu do internetu", **zignoruj je** – to normalne, portal działa lokalnie na urządzeniu.

### 2.2 Wpisanie danych w portalu

1. Po połączeniu otwórz przeglądarkę i wejdź na adres:

   ```
   http://192.168.4.1
   ```

   > Większość urządzeń otworzy stronę konfiguracji **automatycznie** (tzw. captive portal).

2. Wypełnij formularz:

   | Pole | Co wpisać |
   |---|---|
   | **WiFi SSID** | nazwa Twojej sieci domowej (2.4 GHz) |
   | **WiFi Password** | hasło do Twojej sieci |
   | **YouTube API v3 key** | klucz API (jeśli już ustawiony, zostaw bez zmian) |
   | **YouTube channel id** | identyfikator kanału (jeśli już ustawiony, zostaw bez zmian) |
   | **YouTube channel name** | nazwa kanału wyświetlana na matrycy (jeśli już ustawiona, zostaw bez zmian) |

3. Kliknij przycisk **SAVE** (Zapisz).

4. Urządzenie **zrestartuje się**, połączy z Twoją siecią WiFi i po chwili na matrycy pojawi się napis **„WiFi OK"**, a następnie statystyki kanału.

> ● Zniknięcie sieci `AutoConnectAP` z listy dostępnych sieci oznacza, że urządzenie połączyło się z Twoim WiFi – konfiguracja zakończona sukcesem.

---

## 3. Zmiana sieci lub hasła WiFi

Urządzenie automatycznie pokazuje portal konfiguracyjny, gdy **nie może połączyć się z zapisaną siecią** (np. zmiana hasła, nowy router). Wtedy:

1. Powtórz kroki z sekcji **2.1** (połącz się z `AutoConnectAP`, hasło `password`).
2. W portalu podaj **nowe dane sieci WiFi** i kliknij **SAVE**.
3. Urządzenie zrestartuje się i połączy z nową siecią.

> **Nie ma zapisanej sieci** – jak odróżnić tryb portalu od zwykłego trybu pracy?
> - Portal działa tylko kilka minut od uruchomienia (limit czasu połączenia).
> - Podczas pracy urządzenia matryca świeci i po chwili wyświetla statystyki – nie ma potrzeby konfigurować WiFi.

### 3.1 Wymuszona konfiguracja („od zera")

Aby całkowicie usunąć zapisane dane i uruchomić konfigurację od początku:

1. Odłącz zasilanie.
2. Podłącz ponownie zasilanie, a **zaraz po starcie** (w ciągu pierwszych ~5 sekund) wykonaj reset lub przytrzymaj przycisk reset na płytce (jeśli jest).
3. Jeśli to nie pomoże, otwórz plik `src/main.cpp`, odkomentuj linię:

   ```cpp
   // wifiManager.resetSettings(); // uncomment to reset saved settings
   ```

   wgraj firmware ponownie (PlatformIO) i uruchom urządzenie – zapisane dane zostaną usunięte.

---

## 4. Zmiana kanału YouTube / klucza API

Dane YouTube (klucz API, identyfikator i nazwa kanału) konfigurujesz **w tym samym portalu WiFi** – wystarczy uruchomić go ponownie (sekcja 3) i zmienić odpowiednie pola przed kliknięciem **SAVE**.

- **Klucz API (YouTube API v3 key)** – wymagany do odczytu statystyk. **Nie jest wpisany w kodzie** – podajesz go w portalu konfiguracyjnym (sekcja 2.2). Klucz własny wygenerujesz w konsoli Google Cloud (API YouTube Data v3).
- **Channel id** – identyfikator kanału (najlepiej wziąć z adresu URL kanału: `youtube.com/channel/UC...`).
- **Channel name** – nazwa wyświetlana na początku cyklu na matrycy.

---

## 5. Rozwiązywanie problemów

| Objaw | Przyczyna / rozwiązanie |
|---|---|
| Nie mogę znaleźć sieci `AutoConnectAP` | Urządzenie ma już zapisaną sieć i działa normalnie – **albo** minął czas oczekiwania na portal. Odłącz zasilanie, włącz ponownie i od razu szukaj sieci. |
| Łączy się z hotspotem, ale nie otwiera się strona | Wpisz ręcznie adres `http://192.168.4.1` w przeglądarce. Upewnij się, że telefon nie przełącza się na inną sieć (wyłącz dane komórkowe). |
| Nie łączy się z moją siecią domową | Upewnij się, że router nadaje **2.4 GHz** (ESP8266 nie obsługuje 5 GHz). Sprawdź hasło w portalu, uwzględnij wielkie litery i znaki specjalne. |
| Matryca świeci „WiFi OK", ale brak statystyk | Sprawdź klucz API i ID kanału w portalu. Upewnij się, że klucz ma włączone API YouTube Data v3 (bez limitu dziennego). |
| Zmieniłem hasło WiFi, urządzenie nie łączy | Uruchom portal ponownie (sekcja 3) i podaj nowe hasło. |

---

## 6. Uwagi techniczne

- Projekt budowany narzędziem **PlatformIO** (konfiguracja w `platformio.ini`, środowisko `d1_mini_pro`, platforma `espressif8266`).
- Konfiguracja zapisywana jest w pliku `/config.json` w pamięci **LittleFS** (poprzez buforowanie w pamięci RTC przy restarcie).
- Dane WiFi i API są przechowywane lokalnie na urządzeniu – nie są wysyłane nigdzie poza Twoją sieć (z wyjątkiem zapytań do API YouTube i serwera statystyk).
- Oprogramowanie: `src/main.cpp`, biblioteki: `WiFiManager`, `MD_Parola`, `MD_MAX72XX`, `YoutubeApi`, `ArduinoJson`.

---

*Instrukcja obsługi dla użytkownika końcowego – konfiguracja WiFi jest najważniejszym krokiem pierwszego uruchomienia.*
