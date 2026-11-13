# Clocks

[English](README.md) · [Architektura](../../ARCHITECTURE.pl.md)

Clocks obsługuje zestaw wskaźników i mierzy to, czego nie odczytuje ECU.
Generuje przebiegi prostokątne dla fabrycznego prędkościomierza, obrotomierza
i wskaźnika oleju, steruje kontrolkami oleju i ładowania, rysuje dodatkowe
wskazania na TFT i obsługuje buzzer. Mierzy ciśnienie oleju czujnikiem
rezystancyjnym, prędkość przez impulsy ABS oraz temperatury spalin przed DPF
i wewnątrz niego przez dwa MCP9600 na własnym I²C.

## Przygotowanie i uruchomienie

Sprawdź [płytkę i połączenia](../../Fiesta_pcbs/dashboard/Clocks/README.pl.md). Zasilaj moduł
z USB albo instalacji samochodu, nigdy z obu naraz. Przygotuj build według [wspólnej
instrukcji](../../doc/BUILDING.pl.md), otwierając folder `src/Clocks`. Po wgraniu sprawdź
ekran, lampki i ważność danych. Brak ECU lub GPS nie zatrzymuje uruchomienia ekranu.

## Odczyt kontrolek i ekranu

Kontrolka oleju świeci przy braku ciśnienia i miga z częstotliwością 1 Hz,
gdy czujnik jest zwarty albo przerwany, czyli gdy wskaźnik oleju pokazuje ERR.
Od włączenia zasilania do pierwszego odczytu świeci.

Kontrolka ładowania korzysta z obrotów silnika i napięcia akumulatora z ECU.
Świeci, gdy silnik stoi albo te dane są nieaktualne, i gaśnie, gdy po
rozruchu napięcie potwierdzi ładowanie. Zapala się ponownie po zaniku
ładowania i miga z częstotliwością 2 Hz przy zbyt wysokim napięciu. Progi
i czasy potwierdzeń są w [config.h](../../src/Clocks/config.h); wskaźnik napięcia
na TFT ma własny zakres.

Clocks wygasza RPM po 300 ms bez poprawnej ramki `0x127`, niezależnie od
pozostałego ruchu ECU. Rdzeń 0 zatrzymuje obrotomierz i publikuje zero ze
skasowaną flagą ważności RPM. TFT pokazuje N/A do kolejnej poprawnej ramki
RPM; odebrane zero pozostaje ważnym pomiarem. Utrata i powrót pomiaru
przerysowują pole RPM w następnym obiegu pętli ekranu, a zwykłe aktualizacje
liczby pozostają raz na sekundę. Wygaszenie zapala też kontrolkę ładowania,
która po powrocie RPM ponownie potwierdza ładowanie.

Odczyt EGT na TFT pokazuje temperaturę przed DPF. Co 5 s zmienia się
z temperaturą wewnątrz DPF tylko wtedy, gdy podłączony moduł DPF zgłasza
regenerację w ramce `0x124`.

Piktogram GPS na TFT ma zieloną kropkę przy fixie, a bez niego czerwoną,
migającą raz na sekundę. Odbiornik nadaje NMEA także bez fixa. Gdy przez 3 s
nie przyjdzie żadne zdanie z poprawną sumą kontrolną, Clocks uznaje go za
nieobecny: zamiast prędkości pojawia się N/A, jak przy brakującej termoparze,
a kropka miga dwa razy szybciej.

## Testowanie wyjść

Połącz [SerialConfigurator](../SerialConfigurator/README.pl.md) i wybierz zakładkę Tests modułu Clocks.

Konfigurator uruchamia pięć testów fabrycznych zegarów: każda kontrolka
świeci, gaśnie i miga, obrotomierz i prędkościomierz przechodzą przez
ustalone wartości i wracają do zera, a buzzer odtwarza trzy sygnały. Krok
trwa 2 s, o ile konfigurator nie zmieni `step_ms`. Test podstawia tylko
wyjście, którym steruje. Pomiary, ramki CAN i pozostałe wyjścia działają
dalej, a wyjście wraca do pomiaru, gdy test się skończy, zostanie zatrzymany
albo straci kontakt z konfiguratorem. Testy są w każdym obrazie Clocks;
polecenia i klucze statusu podaje
[opis protokołu](../../src/common/scDefinitions/PROTOCOL.pl.md#testy-funkcjonalne).

## Sprawdzenie wejścia ABS

Log debugowania USB podaje częstotliwość impulsów ABS po każdym oknie
trwającym co najmniej 1000 ms, także dla zera i niezmiennej częstotliwości:
`ABS pulses: 1000.00 Hz, delta=1000, window_ms=1000, total=25000`.
Hz to przyrost `impulseCount` podzielony przez rzeczywisty czas pomiaru;
spóźniony obieg pętli wydłuża okno. Licznik zlicza zbocza narastające na GP15,
po jednym na okres sygnału wejściowego. Odczyt należy porównać z generatorem
ABS lub zewnętrznym pomiarem częstotliwości; impulsy pominięte przez
przerwanie GPIO nie trafiają do licznika. Rozdzielczość zliczania wynosi
jeden impuls na okno. Otwarta sesja konfiguratora wycisza log debugowania.

## Przepływ danych i czasy

Pracę dzielą dwa rdzenie RP2040. Rdzeń 0 odbiera i nadaje CAN, mierzy prędkość
z ABS oraz steruje wskaźnikami, kontrolkami i buzzerem. Rdzeń 1 rysuje TFT,
odczytuje czujnik oleju i obie termopary oraz obsługuje sesję konfiguratora.
Rdzenie wymieniają kopie swojego stanu pod krótką blokadą. CAN i TFT dzielą
SPI; ekran zwalnia magistralę między pasmami pikseli o ograniczonej wielkości.
Gdy sterownik CAN nie odpowiada przy starcie, rdzeń 0 ponawia próbę co sekundę,
a ekran i wskaźniki działają.

ECU i Clocks używają automatycznej retransmisji na głównej magistrali CAN,
więc przegrany arbitraż nie porzuca aktualizacji. HAL ogranicza czas każdego
synchronicznego nadania i przerywa transmisję, która nie zakończyła się w tym czasie.
ECU przechowuje najnowszą migawkę każdej okresowej ramki stanu, pedału i turbo.
Nadaje po jednej oczekującej ramce, z odstępem co najmniej 2 ms od swojej
poprzedniej próby nadania. Pętla rdzenia 0 daje pierwszeństwo RPM. Numery sekwencji
odpowiadają faktycznej kolejności nadawania; nowsza migawka zastępuje niewysłaną
ramkę o tym samym ID.
Szybkie odczyty czujników i okresowe migawki stanu zachowują ustaloną siatkę
czasu po opóźnionym obiegu pętli. Zaległe terminy są pomijane.

Clocks nadaje jasność ekranu, ciśnienie oleju i prędkość z ABS co 50 ms, a obie
temperatury spalin co 250 ms. Każdy pomiar ma flagę ważności. Brak termopary
albo zwarty lub przerwany czujnik oleju trafia na magistralę ze skasowaną
flagą, więc odbiorca nie odczyta go jako zera.

Rdzeń 1 odczytuje GPS przez PIO/software serial: TX odbiornika łączy się
z GP27, a RX z GP28, przy 9600 baud. Clocks uruchamia się normalnie bez
odbiornika i fixa.

Współrzędne pozostają w `double` do przeliczenia na mikrostopnie dla
`SC_GET_GPS` i CAN. Lokalny GPS pozostaje dostępny po utracie komunikacji
z ECU. ECU wysyła dawne pola GPS ramki `0x129` jako zera, a Clocks ich
nie odczytuje. Odczyt działa w każdym
obiegu, a nowe dane i zmiany ważności wyzwalają publikację migawki.
Odświeżanie ekranu korzysta z przeliczonej wcześniej prędkości.

Ostatnia pozycja wygasa po 4 s. UTC pochodzi z jednego zdania RMC z fixem
i wygasa po 2 s. Clocks zachowuje UTC dla CAN i SC oraz nadaje go z sekundami
i ważnością w `0x131` co sekundę, przesunięty o wiek zdania do chwili nadania.

Co sekundę Clocks wysyła też pozycję z prędkością GPS (`0x12D`) oraz pozycję
z datą i czasem UTC z dokładnością do minuty (`0x12E`). Jego device tree
odwołuje się do bindingu GPS w `src/common/bindings/`.

Kontroler CAN i TFT współdzielą SPI. Ekran zapisuje dane pasami o rozmiarze
`HAL_DISPLAY_SPI_MAX_TRANSFER_BYTES` z
[hal_project_config.h](../../src/Clocks/hal_project_config.h), więc CAN dostaje
magistralę między dwoma pasami.

## Mapa źródeł

| Plik | Zadanie |
|---|---|
| [Cluster.cpp](../../src/Clocks/Cluster.cpp) | Generuje sygnały fabrycznych wskaźników. |
| [Gauge.h](../../src/Clocks/Gauge.h), [simpleGauge.cpp](../../src/Clocks/simpleGauge.cpp), [tempGauge.cpp](../../src/Clocks/tempGauge.cpp), [pressureGauge.cpp](../../src/Clocks/pressureGauge.cpp) | Przeliczają wskazania. |
| [TFTExtension.cpp](../../src/Clocks/TFTExtension.cpp) | Rysuje ekran TFT. |
| [logic.cpp](../../src/Clocks/logic.cpp) | Uruchamia oba rdzenie i ich pętle; zamienia dane CAN na stan wskaźników, ekranu i buzzera. |
| [clocksState.cpp](../../src/Clocks/clocksState.cpp) | Przekazuje kopie stanu między rdzeniami. |
| [speed.cpp](../../src/Clocks/speed.cpp) | Przelicza częstotliwość impulsów ABS na prędkość. |
| [oilPressure.cpp](../../src/Clocks/oilPressure.cpp) | Przelicza ADC na bary i wykrywa zwarcie lub przerwę czujnika. |
| [egt.cpp](../../src/Clocks/egt.cpp) | Odczytuje oba MCP9600 i ponownie szuka brakującego układu. |
| [gps.cpp](../../src/Clocks/gps.cpp) | Odczytuje lokalny GPS, kontroluje świeżość UTC i koduje ramki GPS. |
| [chargeLamp.cpp](../../src/Clocks/chargeLamp.cpp) | Wyznacza stan kontrolki ładowania z obrotów i napięcia. |
| [oilLamp.cpp](../../src/Clocks/oilLamp.cpp), [lampBlink.h](../../src/Clocks/lampBlink.h) | Wyznacza stan kontrolki oleju z czujnika ciśnienia; faza migania obu kontrolek. |
| [buzzer.cpp](../../src/Clocks/buzzer.cpp), [buzzerStrategy.cpp](../../src/Clocks/buzzerStrategy.cpp) | Generują tony i ostrzeżenia. |
| [alerts.cpp](../../src/Clocks/alerts.cpp) | Zgłasza ostrzeżenia temperaturowe przez strategię buzzera; milczy, gdy buzzer należy do testu zegarów. |
| [clusterOutput.cpp](../../src/Clocks/clusterOutput.cpp) | Utrzymuje jedyny egzemplarz `Cluster` i podaje mu zmierzoną prędkość i obroty albo wartość trwającego testu. |
| [testOutputs.cpp](../../src/Clocks/testOutputs.cpp) | Podstawia stany testowe na wyjścia wskaźników, kontrolek i buzzera; pomiary płyną dalej. |
| [clocksTests.cpp](../../src/Clocks/clocksTests.cpp) | Pięć testów zegarów (`charge`, `oil`, `rpm`, `speed`, `buzzer`) na wspólnym runnerze, uruchamianych operacjami konfiguratora. |
| [can.cpp](../../src/Clocks/can.cpp) | Odbiera CAN i wysyła ramki jasności, oleju z prędkością, EGT, pozycji GPS oraz UTC z GPS; filtry MCP2515 przepuszczają identyfikatory Fiesty. |
