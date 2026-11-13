# Jak ECU steruje nastawnikiem VP37

[English](CONTROL.md) · [Uruchomienie ECU](../README.pl.md)

Regulator ustawia położenie nastawnika pompy. Żądanie kierowcy przechodzi przez rampę, a
regulator porównuje jej wynik z pomiarem Adjustometera. Mapa podtrzymania podaje wstępne
wysterowanie; człony P, I i D korygują błąd. Korekcja prądu, napięcia i rezystancji
dostosowuje wynik do warunków elektrycznych.

## Jak czytać wielkości

| Wielkość | Znaczenie |
| --- | --- |
| Żądane położenie, % | Udział użytecznego skoku. Domyślnie 100% żądania odpowiada 90% skalibrowanego skoku; granicę zmienia limit skoku fizycznego (`X13`). Nie jest to pomiar dawki paliwa w mg/skok. |
| Położenie fizyczne, % | Udział pełnego skoku między skalibrowanym minimum a maksimum. Mapy i reguły górnego zakresu używają tej skali. |
| Sygnał położenia i uchyb, Hz | Wielkość z czujnika częstotliwościowego Adjustometera. Próg uchybu w Hz nie jest częstotliwością pracy regulatora. |
| PWM, Hz | Częstotliwość przełączania stopnia mocy: 200 Hz. |
| PWM, counts | Wysterowanie w jednostkach regulatora; nie należy czytać go bezpośrednio jako procentu wypełnienia. |
| P / Kp | P to bieżący wkład proporcjonalny do wysterowania, Kp to jego wzmocnienie. Analogicznie I jest stanem korekcji, a Ki jej nastawą. |

Kolejne rozdziały opisują tor żądania, utrzymanie położenia i korekcje elektryczne.
[Instrukcja stanowiska](BENCH.pl.md) opisuje polecenia oraz zapis pomiaru;
[implementacja](IMPLEMENTATION.pl.md) opisuje rdzenie, synchronizację i pamięć.

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

### Nadążanie za zmianą żądania

Pierwsze żądanie po starcie rozpoczyna
rampę od skalibrowanego dołu, gdzie kalibracja pozostawia nastawnik.

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
nieruchomego. Tłumienie ustalonego celu jest opcjonalne (`H`, domyślnie
wyłączone). Jawnie ustawiona pochodna osiąga pełną wagę położenia między
85% a 90%, włącza się po ustaleniu celu i zanika przez 50 ms po wznowieniu ruchu.

Mapa podtrzymania i kompensacja napięcia/temperatury obowiązują w całym
zakresie. Po ustaleniu żądania dolna granica wspomagania wznoszenia podąża
za ujemną wyuczoną korektą całki; zejście poniżej celu zachowuje ograniczenie
potrzebne do postoju. Ruchome żądania zachowują pierwotną granicę.
Telemetria podaje `mode:position` i osobne `test:<name|none>`;
nie oznacza potencjometru jako osobnego trybu regulatora.

Wspomaganie w górę ma pełną wartość do 75% skalibrowanego skoku, liniowo
zanika do 85% i pozostaje wyłączone wyżej, ograniczając przyspieszenie
w wrażliwym obszarze. Mapa podtrzymania, wspomaganie w dół, rampa i API
zachowują swoje zadania.

### Postój w górnej części skoku

Przy stałym celu powyżej 85% skalibrowanego skoku poprawne sprzężenie oraz
aktywne kompensacje prądu i rezystancji pozwalają włączyć profil postoju.
Opóźnienie fazy przechwycenia maleje według istniejącego przejścia górnego
zakresu: od trzech sekund przy 85% do zera przy 90%. Wcześniej całkowanie,
strefa martwa i potwierdzanie blokady zachowują zwykłe reguły. W fazie
przechwycenia całkowanie zwalnia o połowę, strefa zwęża się do ±12 Hz,
a potwierdzenie blokady ma najwyżej 5 ms i następuje w kroku regulatora.
Pomijane są wtedy starsza bramka średniego błędu i zwolnienie przez uchyb
średni. Duży utrzymujący się błąd nadal zwalnia blokadę.

Po trzech sekundach spełniania warunków ciągłe całkowanie ustaje,
a wzmocnienie proporcjonalne Kp zwiększa się w spokojnych oknach, nie częściej niż co
1,5 s, do trzykrotności ustawionego wzmocnienia przy 90% skoku. Dodatkowy
udział maleje do zera przy 85%. Jeden przyrost nie przekracza 0,01 PWM/Hz,
a równoczesne przesunięcie do I najwyżej 2,5 nominalnego counta PWM zachowuje
sterowanie. Dodatkowa górna pochodna maleje wraz ze wzrostem sztywności.
Ruchomy cel przywraca zwykłe P, także gdy limit całki nie pozwala w pełni
skompensować tej zmiany.

Jedna aktualizacja przy spokojnej pozycji albo przenosi zmianę wzmocnienia,
albo koryguje bias całki. Parowane przeniesienie zachowuje napęd; korekta
bias czeka na późniejszy przedział bez zmiany wzmocnienia.
Spokojna pozycja oznacza wariancję najwyżej 225 Hz²
w aktualnym oknie od 0,3 do 0,6 s, z bieżącą próbką sprzężenia. Statystyka
odświeża się w każdym kroku regulacji. Aktualizacje następują nie częściej
niż co 1,5 s i używają 5% ustawionego Ki w całym zakresie postoju.
Krok korekty nie przekracza trzech
nominalnych countów PWM i wynika z uśrednionego błędu poza ±12 Hz.
Ciągłe całkowanie pozostaje zablokowane; osobne kroki korekty usuwają ustalony
uchyb w granicach limitów całki i sterowania. Pola `uh`, `rkp`, `ierr`
oraz `I` pokazują spełnienie warunków profilu, efektywne wzmocnienie, wejście ciągłego
całkowania i zachowaną korektę. Profile badawcze zachowują własne reguły.

### Postój w dolnej i środkowej części skoku

Poza górnym profilem postoju 100 ms przy ustalonym celu w paśmie ±12 Hz
zamraża całkę, aby nie zwiększać
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

Temperatura paliwa jest pomiarem ze znakiem; prawidłowe zero i wartość ujemna
nie oznaczają awarii czujnika. Obecny model sterowania korzysta z poprawnych
odczytów powyżej 0 C i do 120 C. Poza tym zakresem lub przy błędzie czujnika
zachowuje ostatni mnożnik temperatury (po starcie równy 1) i niższy limit
dodatniej korekty PID. Tor zmierzonej rezystancji oraz ograniczenia mnożnika
pozostają bez zmian. Obsługa ujemnej temperatury w telemetrii nie potwierdza
działania zimnej pompy; to wymaga pomiarów na wychłodzonym układzie.


Kompensacja skaluje całe feedforward i PID przez `12 V / compensated voltage`. Bloki skanu
ADC docierają niezależnie od kroku regulacji pozycji. Rdzeń 1 odbiera je między krokami
położenia, także między obsługą turbo, RPM i testów poza osłoną terminu, oraz śledzi zbocza
bramki przez granice bloków. Każdy kod ADC jest kompensowany raz przy odbiorze (tablica
korekcji RP2040; kody RP2350 bez zmian). Krok 5 ms redukuje każdy zakończony impuls raz i
odświeża ostatnie okno napięcia w każdym kroku.

Średnia obejmuje ostatni okres PWM bez oczekiwania na zbocze prądu.
Gdy ma mniej niż 20 ms, ograniczona predykcja uwzględnia wiek środka okna
w chwili kroku i pół okresu PWM do zatrzaśnięcia wyjścia. Nachylenie napięcia
narasta przez filtr 20 ms, nie przekracza ostatniego zmierzonego nachylenia
i szybko maleje przy zwolnieniu lub odwróceniu rampy. Przesunięcie predykcji
jest ograniczone do ±0,5 V i zmienia tylko skalowanie polecenia. Lokalny
odczyt ADC używany zastępczo ma filtr 50 ms bez predykcji. Pomiar prądu
oraz estymacja rezystancji używają osobnej średniej z tego samego okresu PWM co prąd.

### Wyznaczanie rezystancji cewki

Estymator rezystancji używa historycznego dopasowania do zatrzaśniętego PWM,
tak jak pętla prądowa. Każde polecenie zapisuje, czy położenie, podane PWM
i zasilanie pozostawały spokojne przez 150 ms po końcu rampy żądania.
Uczenie wymaga spełnienia tych warunków w chwili pomiaru oraz nadal spokojnego stanu bieżącego.
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
PWM się zawija i zatrzaskuje compare. Historia ADC obejmuje 3,25 okresu PWM. Pomiar prądu zamyka się po potwierdzeniu
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

## Badawczy profil regulacji

Opcjonalny profil pełnej kalibracji służy badaniom na stanowisku.
Zwykły start i reset nastaw zachowują domyślny limit skoku; zbudowanie lub wgranie firmware
nie włącza profilu badawczego.

Komendy stanowiska wybierają fizyczny limit skoku (`X13`), blokadę całki
zależną od ruchu (`X18`), wagę zadania (`X21`), górne pasmo całki (`X23`),
hamowanie rampy (`X24`) oraz pomoc podczas zejścia (`X25`, domyślnie wyłączona).
Pozostawiany udział korekty zejścia (`X26`) wynosi od 0,25 do 0,75,
domyślnie 0,4. Działa przy włączonej pomocy podczas zejścia (`X25`),
gdy dla celu nie ma wyuczonej korekty postoju. Opcje korzystają ze wspólnej ścieżki zadawania pozycji i zachowują limity
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

## Dlaczego ograniczono wspomaganie dojazdu

Poniższe obserwacje stanowiskowe uzasadniają nastawy. Nie są kryteriami odbioru ani gwarancją wyniku dla nowego firmware.

Śledzone żądanie zatrzymane na szczycie
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
samej rampy zwiększało wyprzedzenie.
