# Fingerprint Gate (UART fingerprint + PIN unlock)

 C projekat koji radi kao **kontrola pristupa**: u pozadini stalno skenira prst preko **UART fingerprint modula (Waveshare/R503-kompatibilan protokol)**, a zatim kroz CLI komande (uz PIN) omogućava:
- **`unlock`**: otključavanje “gate”-a ako je poslednji sken validan i korisnik je u whitelist bazi
- **`add`**: enrollment (upis) novog prsta i dodavanje u `users.txt`
- **`list`**: prikaz korisnika iz baze

> Napomena: u ovom repou je `gate_stub.c` (simulacija bez GPIO). Na realnom uređaju treba zameniti `gate_stub.c` implementacijom koja upravlja bravom/relejem (GPIO).

---

## Kako radi (kratko)

- **Scanner thread (pozadinski)**:
  1. proverava da li je prst prisutan (`fp_finger_detect`)
  2. uzima sliku (`fp_get_image`, uz retry)
  3. generiše template u RAM buffer (`fp_generate`)
  4. radi pretragu kroz memoriju modula (`fp_search`)
  5. ako nađe ID → pamti *poslednji match* + timestamp (za prozor autorizacije)

- **CLI/PIN thread (main loop)**:
  - prima komande sa tastature (`help`, `list`, `add <PIN> <Name>`, `unlock <PIN>`, `exit`)
  - `unlock` proverava:
    - da je PIN tačan
    - da postoji “recent scan” (u zadnjih `AUTH_WINDOW_SECONDS`)
    - da je ID u `users.txt`
  - ako je sve OK → `gate_open_ms(GATE_OPEN_MS)`

- **Enrollment (`add`)**:
  - pauzira scanner thread (`g_pause_scanner`) i zaključava mutex nad modulom
  - traži da isti prst prisloniš **3 puta** (1/3, 2/3, 3/3), uz “remove finger” između
  - radi `fp_merge` i `fp_store` u memoriju modula
  - dopisuje korisnika u `users.txt`

---

## Requirements

- Linux (tipično Raspberry Pi OS)
- `gcc`, `make`
- `pthread` (linkuje se sa `-lpthread`)
- UART omogućen i fingerprint modul spojen na UART

---

## Hardware / UART setup (Raspberry Pi)

1. Poveži modul na UART:
   - **TX modula → RX Pi**
   - **RX modula → TX Pi**
   - **GND → GND**
   - **VCC** prema specifikaciji modula (često 3.3V ili 5V, proveri datasheet!)

2. Omogući UART i isključi serial console (na Pi OS):
   - `sudo raspi-config`
     - Interface Options → Serial Port
       - “Login shell over serial?” → **No**
       - “Enable serial port hardware?” → **Yes**
   - reboot

3. Proveri uređaj:
   - default je `SERIAL_PORT "/dev/serial0"` (vidi `config.h`)
   - korisno: `ls -l /dev/serial0`

4. Dozvole:
   - `sudo usermod -aG dialout $USER`
   - logout/login

---

## Build

Ako je projekat organizovan kao u Makefile-u (sa `src/` folderom):

```bash
make
```

Ovo pravi binarni fajl: `finger_gate`

Čišćenje:
```bash
make clean
```

> Ako su ti `.c` fajlovi trenutno u root-u, a Makefile očekuje `src/`, ili prebaci fajlove u `src/` ili promeni `SRC=` u Makefile-u.

---

## Run

Pokretanje (na uređaju sa UART modulom):
```bash
./finger_gate
```

Program će:
- inicijalizovati UART
- pingovati fingerprint modul (`fp_init`)
- startovati scanner thread
- otvoriti CLI prompt za komande

Za prekid:
- `exit` ili `Ctrl+C`

---

## Komande

- `help` – ispis komandi
- `list` – ispis sadržaja `users.txt`
- `add <PIN> <Name>` – upis novog prsta (enrollment)
- `unlock <PIN>` – otključavanje ako je poslednji sken validan
- `exit` – izlaz

Primer:
```bash
list
add 1234 Jovan Nikolic
unlock 1234
```

---

## Konfiguracija (`config.h`)

Najbitnije opcije:

- `SERIAL_PORT` – npr. `"/dev/serial0"`
- `BAUDRATE` – trenutno 115200
- `ACCESS_PIN` – PIN potreban za `add` i `unlock`
- `AUTH_WINDOW_SECONDS` – koliko dugo važi poslednji sken (default 8s)
- `GATE_OPEN_MS` – koliko dugo “gate” ostaje otvoren
- `USERS_FILE` – fajl sa whitelist korisnicima (`users.txt`)
- `LOG_FILE` – log fajl (`logs/scans.log`)
- `SEARCH_START_ID` / `SEARCH_END_ID` – opseg ID-jeva u memoriji modula
- Enrollment retry parametri:
  - `ENROLL_GET_IMAGE_RETRIES`
  - `ENROLL_GET_IMAGE_RETRY_DELAY_MS`

---

## Baza korisnika (`users.txt`)

Format svake linije:
```
<id>,<name>,<role>
```

Primer:
```
1,Jovan,user
2,Stefan,user
```

- `id` je ID template-a u memoriji fingerprint modula
- `role` je trenutno hardkodovan na `"user"` pri enrollment-u

---

## Logovanje

- Log se piše u `logs/scans.log` (kreira se folder `logs/` ako ne postoji)
- Loguje se:
  - start/stop
  - scan rezultati (`SCAN matched_id=...`, `SEARCH no match`)
  - enrollment koraci i greške
  - ACCESS GRANTED / DENIED

---

## Struktura fajlova

- `main.c` – threads, CLI komande, enrollment, autorizacija
- `uart.c/.h` – UART open/read/write (termios + select timeout)
- `waveshare_fp.c/.h` – driver/protokol za fingerprint modul (26B paketi + checksum)
- `user_db.c/.h` – rad sa `users.txt`
- `gate_stub.c`, `gate.h` – simulacija otključavanja (zameniti stvarnim GPIO kodom)

---

## Troubleshooting

**1) “UART open failed (/dev/serial0)”**
- UART nije uključen ili device ne postoji
- proveri `raspi-config` i `ls -l /dev/serial0`

**2) “Fingerprint init failed”**
- pogrešno ukršteni RX/TX
- pogrešan baudrate
- modul nema napajanje ili je nekompatibilan protokol
- proveri i `SENSOR_SID` / `SENSOR_DID` (default 0/1)

**3) Skener “ne hvata” prst**
- proveri da li `fp_finger_detect` vraća prisustvo
- proveri stabilno napajanje (moduli znaju da budu osetljivi)

**4) Gate je samo simulacija**
- to je očekivano: `gate_stub.c` samo ispisuje poruke.
- napravi `gate.c` sa GPIO upravljanjem i promeni Makefile da linkuje `gate.c` umesto `gate_stub.c`.


