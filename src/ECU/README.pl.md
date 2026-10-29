# ECU i sterowanie VP37

[English](README.md) · [Architektura systemu](../../ARCHITECTURE.pl.md)

ECU steruje wtryskiem przez pompę VP37, doładowaniem przez N75, świecami
żarowymi, wentylatorem, grzałką bloku i ogrzewaną szybą. Zapisuje DTC
i odpowiada na OBD-II jako Ford Fiesta 1.8 DI EEC-V ECU. Migracja MISRA-C
obejmuje ECU, współdzielony VP37 i Adjustometer; dla dwóch ostatnich bramka
wymaga braku aktywnych znalezisk. Zakres opisuje [MISRA](../../MISRA.pl.md).

Przytoczone obserwacje stanowiskowe wyjaśniają dobór strojenia. Nie są wynikiem
odbioru dowolnego nowego obrazu firmware. Zmiany sterowania wymagają nowych
pomiarów mechanicznego zachowania układu.

## Wykonanie i synchronizacja

- **Rdzeń 0** wykonuje pętlę główną: timery programowe, CAN, OBD-II,
  przekaźniki, zapis DTC i sesję konfiguratora.
- **Rdzeń 1** obsługuje sterowanie VP37, przerwanie RPM i turbinę.

Nie ma RTOS. `start.c` instaluje tabelę par `(period, callback)`, wykonywanych
przez rdzeń 0 od około 10 ms dla szybkich odczytów do sekundy dla obsługi
okresowej. VP37 należy do rdzenia 1. Kroki sterowania biegną po stałej
siatce okresów `pidTimeUpdate`: spóźniony start nie przesuwa następnego
terminu, a start spóźniony o cały okres zaczyna nową siatkę zamiast serii
kroków nadrabiających. W oknie `CORE1_VP37_GUARD_US` przed terminem rdzeń 1
nie zaczyna innej pracy (turbina, RPM, zadanie testu) i czeka na termin
z RAM, więc krok nie dziedziczy ogona tej pracy. Testy stanowiskowe zapisują
ogłoszenia kroków na rdzeniu 1, a drukuje je rdzeń 0; zmienione zadanie testu
czeka na ten wydruk, najwyżej `STEP_EVENT_APPLY_TIMEOUT_MS`, więc w logu
ogłoszenie nadal poprzedza ruch, który zapowiada. Każdy krok publikuje
stan za licznikiem sekwencji; rdzeń 0 kopiuje go bez blokady. Słowa danych są zapisywane
i odczytywane atomowo z porządkiem relaxed, więc kopia nakładająca się na
publikację zostaje odrzucona bez wyścigu danych. Sterowanie nie czeka na
telemetrię ani aktualizację wartości globalnych.

Telemetria stanowiskowa czeka na rdzeniu 0 na zakończenie kroku przed
formatowaniem raportu: pobieranie kodu z flash na tym rdzeniu wydłużało krok
średnio o około 300 µs. Bufor śladu zmienia właściciela przez jeden atomowy
stan. Magistralę I²C z zatrzaskiem PCF8574 i menedżer DTC z pamięcią chronią
muteksy. Flash zapisuje rdzeń 0.

Główne sterowniki VP37, turbo, RPM, wentylatora, grzałki, świec, szyb i stanu
silnika znajdują się w `ecu_context_t` w [ecuContext.h](ecuContext.h).
CAN, czujniki, DTC, czas RTC, OBD i sesja konfiguratora mają stan lokalny w plikach.

## Zadawanie położenia i przygotowanie wejścia

`hal_status_t VP37_setPositionDemandPercentage(VP37Pump *pump, float percent)`
przyjmuje 0-100% użytecznego skoku.
`hal_status_t VP37_setPositionDemandValue(VP37Pump *pump, int32_t value)`
przyjmuje surowe jednostki sprzężenia w granicach
`VP37_getPositionDemandMinValue()` i `VP37_getPositionDemandMaxValue()`.
Użyteczny skok kończy się przy `VP37_PHYSICAL_LIMIT_PERCENT`
skalibrowanego zakresu. Ograniczenie utrzymuje nastawnik poniżej obszarów
ujemnej sztywności u góry, kosztem największych dawek. Wartość 100 przywraca
pełny skok. Limit jest udziałem skoku: powyżej zera i najwyżej 100; pilnuje
tego kompilacja i tak samo komenda stanowiska `X13`.

Martwa strefa całkowania rośnie silnie powyżej 82% skoku, więc postój przy
pełnym żądaniu ustala się poniżej limitu. Oba API zapisują to samo
żądanie: wejście kierowcy, sterowanie silnikiem i testy używają jednej rampy,
feedforward i PID. Różnią się tylko jednostką. Nie ma osobnego trybu źródła,
flagi filtra ani alternatywnej ścieżki położenia. Awaryjny stop i kalibracja
pozostają osobnymi operacjami.

`sensors.c` przygotowuje wejście analogowe co 10 ms: zachowuje ułamkowe
procenty, stosuje filtr 30 ms i martwą strefę 0,1 punktu procentowego,
a wynik udostępnia przez `getDriverDemandPercent()`. Zero trafia do wyniku
bez opóźnienia filtra. Odczytują go bezpośrednie żądanie kierowcy
oraz `engineOperation`; aplikacja wybiera żądanie przekazane VP37.
Opcjonalne sterowanie silnikiem włącza `ECU_ENGINE_CONTROL_ENABLED` w `config.h`.

## Regulacja położenia

VP37 używa PWM 200 Hz, wspólnego z filtrem okresu sterowania Adjustometera. Pojedynczy skok w górę od pierwszego cyklu jest
traktowany jako nieruchomy cel: rampa rośnie 225%/s ze wspomaganiem ruchu
0,6 i hamuje przed celem 750%/s². Nastawnik podąża za rampą i dojeżdża wolniej,
ograniczając przeregulowanie i oscylacje. Cel zmieniony ponownie w ciągu
25 ms jest śledzony do 300%/s bez hamowania. Wspomaganie w górę rośnie
z rampą do 250%/s, a dalej pozostaje stałe.

Zejście zachowuje 300%/s. Pojedynczy skok w dół ma pełne wspomaganie przez
25 ms, potem 0,3. Reguły górnego zakresu zależą od fizycznego położenia:
wspomaganie w górę maleje między 75% a 85%; od 75% zmieniają się zakres
całkowania i martwa strefa. Górne szybkości to 275%/s i 187,5%/s dla celu
nieruchomego. Tłumienie ustalonego celu osiąga pełną wagę położenia między
85% a 90%, z domyślnym wzmocnieniem 0,001 PWM·s/Hz. Włącza się po ustaleniu
celu i zanika przez 50 ms po wznowieniu ruchu. Bez tych reguł szybka rampa
przekraczała limit o jedną dziesiątą skoku, a zejście oscylowało.

Mapa podtrzymania i kompensacja napięcia/temperatury obowiązują w całym
zakresie. Po ustaleniu żądania dolna granica wspomagania wznoszenia podąża
za ujemną wyuczoną korektą całki; zejście poniżej celu zachowuje ograniczenie
potrzebne do postoju. Ruchome żądania zachowują pierwotną granicę.
Telemetria podaje `mode:position` i osobne `test:<name|none>`;
nie oznacza potencjometru jako osobnego trybu regulatora.

Przy ustalonym celu 100 ms w paśmie ±12 Hz zamraża całkę, aby nie zwiększać
siły przeciw tarciu statycznemu; w paśmie ±12 Hz musi być też błąd uśredniony
z 0,3 s, więc przejście przez pasmo w trakcie dojazdu jej nie zamraża. Błąd
przekraczający ±40 Hz rozpoczyna odliczanie 500 ms do zwolnienia całki. Powrót
do ±12 Hz je kasuje; wahania wokół ±40 Hz nie rozpoczynają go od nowa, więc
utrzymujący się dryf zwalnia całkę. Poniżej 75% skoku, gdzie strefa martwa
całki ma bazową szerokość, blokada wchodzi dopiero przy uśrednionym błędzie
w ±6 Hz (nastawa `X8=<Hz>`, 1-12), a uśredniony błąd poza ±12 Hz przez 1 s ją
zwalnia (nastawa `X6=<ms>`, 0 wyłącza).

Gdy pozycja po raz pierwszy znajdzie się w ±40 Hz od ustalonego celu poniżej
75% skoku, mnożnik termiczny nie spada poniżej wartości, z którą nastawnik
dojechał, do zmiany celu (nastawa `X7=0|1`). Dojazd w górę podnosi estymatę
rezystancji toru sterowania, a jej późniejsza relaksacja ściągałaby ustaloną
pozycję poniżej celu przy zamrożonej całce. Nagrzewanie cewki podnosi potem
estymatę i mnożnik idzie za tym wzrostem. Gdy mnożnik jest trzymany, cel pętli
prądu ma tę samą nadwyżkę ponad estymatę, więc pętla nie odbiera dodatkowego
napędu (nastawa `X9=0|1`). Przy zmianie celu mnożnik od razu wraca do
estymaty. Powyżej 75% skoku obie reguły są
wyłączone: tam prowadziły długie postoje do zrywów tarcia.

## Kompensacja napięcia i rezystancji

Kompensacja skaluje całe feedforward i PID przez `12 V / compensated voltage`.
Bloki skanu ADC docierają niezależnie od kroku regulacji pozycji. Rdzeń 1 odbiera je między
krokami położenia i śledzi zbocza bramki przez granice bloków. Każdy kod ADC
jest kompensowany raz przy odbiorze (tablica korekcji RP2040; kody RP2350 bez zmian). Krok 5 ms redukuje każdy zakończony impuls
raz i odświeża ostatnie okno napięcia w każdym kroku.

Średnia obejmuje ostatni okres PWM bez oczekiwania na zbocze prądu.
Gdy ma mniej niż 20 ms, ograniczona predykcja uwzględnia wiek środka okna
w chwili kroku i pół okresu PWM do zatrzaśnięcia wyjścia. Nachylenie napięcia
narasta przez filtr 20 ms, nie przekracza ostatniego zmierzonego nachylenia
i szybko maleje przy zwolnieniu lub odwróceniu rampy. Przesunięcie predykcji
jest ograniczone do ±0,5 V i zmienia tylko skalowanie polecenia. Lokalny
odczyt ADC używany zastępczo ma filtr 50 ms bez predykcji. Pomiar prądu
oraz estymacja rezystancji używają osobnej średniej z tego samego okresu PWM co prąd.

Estymator rezystancji używa historycznego dopasowania do zatrzaśniętego PWM,
tak jak pętla prądowa. Każde polecenie zapisuje, czy położenie, podane PWM
i zasilanie pozostawały spokojne przez 150 ms po końcu rampy żądania.
Nauka wymaga tej kwalifikacji historycznej i nadal spokojnego stanu obecnego.
Opóźniona obserwacja z dojazdu nie staje się poprawna tylko dlatego, że dotarła później.

Pierwszą estymatę tworzą wyłącznie spokojne pomiary. Po uzyskaniu gotowości
również dopasowane pomiary podczas ruchu aktualizują ją z tą samą stałą 2 s.
Obserwowana rezystancja odzwierciedla opóźnienie prądu względem PWM podczas
przemiatania, więc estymata śledzi nagrzewanie cewki i pełni rolę feedforward
ruchu. Wolniejsza lub ograniczana aktualizacja zwiększała błąd cykliczny na stanowisku.

Okno spokoju dopuszcza zmianę położenia o 60 Hz i PWM o 8 od wartości
odniesienia; przekroczenie ponawia okno. Każdy okres prądu aktualizuje powolną
estymatę najwyżej raz. Filtr używa odstępów obserwacji, ograniczonych do 50 ms
po przerwie. Łączna zmiana napięcia ponad 0,1 V wstrzymuje estymację na 100 ms.
Zdrowe nowe pomiary podtrzymują ostatnią gotową korektę podczas długich zmian
napięcia; ich utrata wygasza ją po 10 s. Zmiana zasilania jest wykrywana
z napięcia zmierzonego przed predykcją.

Telemetria pokazuje `vage` (wiek środka okna w µs), `vlead` (przesunięcie
predykcji w V przed dolną granicą 7 V), `vdot` (V/s), `rhold` (wstrzymanie
nauki przez zmianę zasilania lub napędu), `Robs` (ostatnia dopasowana
rezystancja w omach), `rmatch` (dopasowany pomiar), `rquiet` (kwalifikacja
historyczna), `rlearn` (aktualizacja w kroku) i `rlcnt` (licznik aktualizacji).
Niedopasowane obserwacje podtrzymują gotową estymatę bez zmiany jej wartości.
`scanus` mierzy odbiór i redukcję ADC, a `execus` cały krok sterowania;
oba w µs, bez oczekiwania na następny krok. `late` to największe spóźnienie
startu po terminie w ostatnim pełnym oknie 100 kroków, w µs, bez kroków
spóźnionych o cały okres; `ovr` liczy takie kroki od startu. Osłona rdzenia 1
ma pokrywać wartości `late` widziane na stanowisku.

## Sprzężenie prądowe

Ograniczona pętla proporcjonalna koryguje polecenie położenia przed mnożnikami
napięcia i temperatury. Celem jest nominalne feedforward plus PID położenia
wyrażone jako prąd fazy ON z pominięciem marginesów przy zboczach. Przeliczenie
zachowuje odniesienie estymatora rezystancji do surowego napięcia i kalibrację
dzielnika. Bocznik źródłowy nie mierzy recyrkulacji przy wyłączonym MOSFET,
więc cel nie oznacza średniego prądu cewki z całego okresu.

Odwrócony napęd przewodzi na końcu okresu PWM. Obserwacja jest dopasowywana
do polecenia zapisanego przed poprzednim opadającym zboczem prądu, gdy licznik
PWM się zawija i zatrzaskuje compare. Śledzone żądanie zatrzymane na szczycie
(np. pełny pedał) dojeżdża z pełną szybkością. Wspomaganie w górę rośnie do
`VP37_PWM_FF_MOTION_RATE_CAP_PERCENT_PER_S` (250%/s, nastawa `X5`), potem
pozostaje stałe. Przy wspomaganiu dla 300%/s nastawnik wyprzedzał rampę
u góry o 260-500 Hz i oscylował po rozgrzaniu; przy 250%/s wyprzedzenie
wynosiło około 150 Hz i ruch się uspokajał. Z filtrem okresu napędu
Adjustometera przeregulowanie dojazdu wynosiło 100-200 Hz i pozostawało
poniżej obszarów ujemnej sztywności.

Opcjonalny hamulec u góry `VP37_TOP_ARRIVAL_DECEL_PERCENT_PER_S2` (`X2`)
jest wyłączony. Przy 750%/s² spowalniał ostatnie 70% zakresu, a przy 3000%/s²
nadal opóźniał dojazd o 100 ms i pogłębiał późniejsze opadnięcie. Zwolnienie
samej rampy zwiększało wyprzedzenie. Pierwsze żądanie po starcie rozpoczyna
rampę od skalibrowanego dołu, gdzie kalibracja pozostawia nastawnik.

Historia ADC obejmuje 3,25 okresu PWM. Pomiar prądu zamyka się po potwierdzeniu
zbocza opadającego, bez czekania na następne ON. Prąd, wypełnienie i napięcie
używają tego samego przedziału między zboczami opadającymi, niezależnego od
zmiany wypełnienia. Zapisy w odległości do 100 µs od zbocza są niejednoznaczne
i odrzucane, podobnie jak niezgodne wypełnienie, błędne pomiary i wiek środka
ON co najmniej 25 ms.

Wzmocnienie wynosi 0,35 w jednostkach równoważnego nominalnego polecenia.
Korekta jest ograniczona do ±40 jednostek PWM i zmienia się najwyżej
1000 jednostek/s. Granice PID położenia uwzględniają ją przed krokiem PID.
Powtórzone obserwacje nie przeliczają błędu; brak sprzężenia sprowadza korektę
stopniowo do zera, a spoczynek lub stop zeruje ją od razu. Nie ma dodatkowej całki prądu.

## Telemetria

Pola pętli prądowej: `cen` (włączenie), `cuse` (świeży dopasowany pomiar),
`ctar` (ostatni cel w A), `cref` (cel historyczny w A), `cerr` (historyczny
cel minus prąd ON w A), `cpwm` (nominalna korekta) i `cage` (wiek środka ON
w µs). Występują też w IPULSE z `latch`, `lp`, `lduty` i `lv`: odtworzonym
czasem zatrzaśnięcia, okresem, wypełnieniem i poprawnością. `dma`, `take`
i `reduce` to czasy MCU zakończenia DMA, zachowania i redukcji;
`reduce - (us + on)` określa opóźnienie dostarczenia po końcu ON.

Stanowiskowe IWAVE, najwyżej co 100 ms, używają identyfikatora `us` IPULSE
i podają osiem średnich ON w kolejności czasowej z czasami próbek.
Pomijają marginesy przy zboczach i nie mierzą zaniku prądu swobodnego.
`gl` zlicza odrzucone zakłócenia bramki od resetu historii. Obraz stanowiskowy
przyjmuje `C0`/`C1` do porównania sprzężenia wyłączonego i włączonego przez
jedno API położenia. Test modelu elektrycznego nie potwierdza stabilności
mechanicznej; postoje u góry i ruch wymagają walidacji sprzętowej.

Wspomaganie w górę ma pełną wartość do 75% skalibrowanego skoku, liniowo
zanika do 85% i pozostaje wyłączone wyżej, ograniczając przyspieszenie
w wrażliwym obszarze. Mapa podtrzymania, wspomaganie w dół, rampa i API
zachowują swoje zadania.


## Badawczy profil regulacji

Rewizja 148 zawiera opcjonalny profil pełnej kalibracji do badań na stanowisku.
Zwykły start i reset nastaw zachowują domyślny limit skoku; wgranie tej rewizji
nie włącza profilu badawczego.

Komendy stanowiska wybierają fizyczny limit skoku (`X13`), blokadę całki
zależną od ruchu (`X18`), wagę zadania (`X21`), górne pasmo całki (`X23`),
hamowanie rampy (`X24`) oraz pomoc podczas zejścia (`X25`, domyślnie wyłączona).
`X26` wybiera pozostawiany udział korekty zejścia od 0,25 do 0,75, domyślnie
0,4. Opcje korzystają ze wspólnej ścieżki zadawania pozycji i zachowują limity
wyjścia oraz całki.

Przy zejściu pomoc zaczyna działać po zmierzonym ruchu o 0,3 punktu
procentowego. Wycofuje nadmiar ujemnej całki do korekty podtrzymania i kończy
się przy pierwszym wejściu w pasmo całki lub przekroczeniu celu. Dla tego
samego celu wewnątrz skoku można użyć wcześniej zmierzonej korekty po dwóch
sekundach stabilnego postoju bez nasycenia. Końce zakresu nie uczą tej
korekty. Stop, reset i przyjęte zmiany nastaw unieważniają obserwację.
Dla nieznanego celu pozostaje wybrany udział korekty.

Telemetria pokazuje `ibias`, `ianchor`, `ipart` i `iphase` dla zejścia oraz
`ihvalid`, `ihtar`, `ihbias` i `ireuse` dla zapamiętanej korekty postoju.
Profil badawczy wymaga osobnego odbioru sprzętowego przed użyciem jako
zwykłe ustawienia pracy.

## Mapa źródeł

| Plik | Zadanie |
|---|---|
| [start.c](start.c) | Kolejność startu, timery, watchdog i pętle rdzeni. |
| [sensors.c](sensors.c) | Wejścia analogowe, filtrowane żądanie kierowcy, PCF8574 i odczyty I²C. |
| [can.c](can.c) | Główne ramki CAN, w tym publikacja RPM. |
| [obd-2.c](obd-2.c) | Wejście OBD CAN i odpowiedzi ISO-TP. |
| [obd_j1979.c](obd_j1979.c) | Usługi SAE J1979 i PID Mode 01. |
| [obd_ford_diag.c](obd_ford_diag.c) | Ford EEC-V UDS, KWP2000 i SCP. |
| [dtcManager.c](dtcManager.c) | Katalog, zapis i odczyt diagnostyczny DTC. |
| [rpm.c](rpm.c) | Obroty z przerwania czujnika Halla. |
| [common/vp37](../common/vp37/) | Sterownik współdzielony przez ECU i VP37TestBench. Listę [vp37_sources.txt](../common/vp37/vp37_sources.txt) czyta CMake testów, a generator VS Code wpisuje ją do manifestów modułów z `sharedSources`. |
| [vp37.c](../common/vp37/vp37.c) | Start pompy, API żądania i cykl sterowania. |
| [vp37_adapter.c](vp37_adapter.c) | PWM ECU, enable PCF8574, blokada RPM, I²C Adjustometera, start i wartości `F_FUEL_TEMP`/`F_VOLTS` ze stanu publikowanego. |
| [vp37_adjustometer.c](../common/vp37/vp37_adjustometer.c) | Ramki, diagnostyka i oczekiwanie na poziom odniesienia. |
| [vp37_snapshot.c](../common/vp37/vp37_snapshot.c) | Publikowanie i kopiowanie stanu bez blokady. |
| [vp37_feedback.c](../common/vp37/vp37_feedback.c) | Położenie i kalibracja skoku. |
| [vp37_compensation.c](../common/vp37/vp37_compensation.c) | Napięcie, temperatura paliwa i rezystancja cewki. |
| [vp37_control.c](../common/vp37/vp37_control.c) | Mapa podtrzymania, wyuczona korekta, PID, martwa strefa i postój. |
| [vp37_current.c](../common/vp37/vp37_current.c) | Pomiar prądu na boczniku. |
| [vp37_current_control.c](../common/vp37/vp37_current_control.c) | Sprzężenie ON i historia PWM. |
| [vp37_tuning.c](../common/vp37/vp37_tuning.c) | Sprawdzane zakresy nastaw stanowiska. |
| [vp37_telemetry.c](../common/vp37/vp37_telemetry.c) | Próbki sterowania i ślady na rdzeniu 0, tylko obraz stanowiskowy. |
| [turbo.c](turbo.c), [engineMaps.c](engineMaps.c) | Sterowanie doładowaniem i tabela N75. |
| [vp37_maps.c](../common/vp37/vp37_maps.c) | Mapa podtrzymania i zmiany całki oraz martwej strefy. |
| [engineFan.c](engineFan.c), [engineHeater.c](engineHeater.c), [glowPlugs.c](glowPlugs.c), [heatedWindshield.c](heatedWindshield.c) | Wyjścia przekaźnikowe. |
| [engineFuel.c](engineFuel.c) | Poziom paliwa. |
| [rtcTime.c](rtcTime.c) | Czas UTC z ramki `0x130` Fiesta_clock. Oznacza pierwsze wystąpienie DTC i raport restartu przez watchdog; po 3 s bez zaufanej ramki czas jest nieznany. |
| [config.c](config.c) | Ustawienia i sesja konfiguratora. |
| [tests.c](tests.c) | Rejestr testów, sterowane elementy, dostępność sekwencji i konfiguratora. |
| [tests_workers.c](tests_workers.c) | Generatory żądań, czynności jednorazowe i postęp. |
| [tests_helpers.c](tests_helpers.c) | Parametry i runner konsoli/konfiguratora z blokadą RPM i keepalive; bezczynne funkcje bez `ECU_FUNCTIONAL_TESTS_ENABLED`. |

## Obraz stanowiskowy i sprzęt

Testy istnieją tylko w obrazie BENCH zadeklarowanym w
[hal_project_config.h](hal_project_config.h) przez `ECU_FUNCTIONAL_TESTS_ENABLED=1`.
Konfiguracja HAL współdzielona z testami hostowymi jest w
[ecu_hal_config.h](ecu_hal_config.h); deklarację wariantu HAL czyta wyłącznie
z `hal_project_config.h`. W VS Code wybierz `Project: Build variant: BENCH`
lub `Project: Upload variant: BENCH`. Obraz trafia do
`src/ECU/.build/rp2350-arm/rp2350-plus/variants/BENCH/`, a dla montażu
RP2040-Plus pod `.build/rp2040/rp2040-plus/`. Zadania bez wariantu dają obraz
samochodu. Odpowiedniki CLI podaje [instrukcja kompilacji](../../doc/BUILDING.pl.md).

[device_tree.json](device_tree.json) przypisuje piny i adresy. Montaże
`rp2350-plus` (Waveshare RP2350-Plus, domyślny) i `rp2040-plus` (Waveshare
RP2040-Plus) mają wspólne PCB i okablowanie; wybiera je `--assembly` lub
wybór montażu w VS Code. Interfejsy:

- **I²C 400 kHz:** ECU master, Adjustometer `0x57`, PCF8574A `0x38`.
  PCF8574A dopuszcza 100 kHz, więc pracuje poza specyfikacją do wymiany na
  PCA8574A lub wydzielenia magistrali. Typ lokalny w `bindings/` ogranicza
  magistralę do 400 kHz; szybsza konfiguracja nie przejdzie kontroli drzewa.
- **SPI:** dwa MCP2515, CAN0 dla samochodu, CAN1 dla OBD-II.
- **ADC:** sześć wejść przez HC4051 (temperatury płynu, oleju i powietrza,
  przepustnica, paliwo, ciśnienie dolotu), zasilanie oraz bocznik VP37 na
  GPIO26 zarezerwowanym dla tego pomiaru. Skan prądu VP37 zajmuje ADC;
  pozostałe odczyty analogowe korzystają wtedy z jego bufora.
- **PWM:** VP37 i N75; **GPIO:** kontrolka DPF; **przerwanie GPIO:** Hall
  silnika.
- **EEPROM emulowany we flash:** DTC i ustawienia.

## Budżet RAM sterowania

ECU zachowuje prąd i napięcie w pierścieniu dwóch kanałów; multiplekser
czujników pozostaje częścią skanu DMA z trzema kanałami. Redukcja prądu
oraz śledzenie zboczy używają `HAL_RAM_FUNC`, ograniczając pobieranie kodu
z flash. Ślad stanowiskowy mieści 1024 pełne próbki z flagami zapisanymi
jako bity. Długość określa `VP37_TRACE_SAMPLES`. Po zmianie buforów lub kodu
w RAM sprawdź mapy pamięci ELF obu wariantów firmware.
