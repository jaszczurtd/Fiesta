# Fiesta

[English](README.md)

Firmware i elektronika do Forda Fiesty 1.8 diesel z niestandardowym silnikiem
łączącym części 1.8D z wtryskiem pośrednim i 1.8TDDI z wtryskiem bezpośrednim
oraz pompą VP37. [JaszczurHAL](https://github.com/jaszczurtd/JaszczurHAL)
zapewnia obsługę sprzętu i narzędzia. Autor: Marcin „Jaszczur" Kielesiński.

Projekt jest na etapie prototypu. Cały system nie został sprawdzony jako
zintegrowana instalacja w samochodzie drogowym. Próby ECU i VP37 odbywają się
na osobnym silniku na stalowej ramie. Clocks i funkcje nadzorcze ECU były
używane w samochodzie bez sterowania VP37.

## Moduły

| Moduł | Przeznaczenie |
|---|---|
| [ECU](src/ECU/README.pl.md) | Sterowanie silnikiem, VP37 i doładowaniem, przekaźniki, diagnostyka i zapis DTC. |
| [Clocks](src/Clocks/) | Fabryczne wskaźniki, dodatkowy ekran TFT i ostrzeżenia dźwiękowe. |
| [OilAndSpeed](src/OilAndSpeed/) | Pomiar ciśnienia oleju, prędkości i temperatur spalin. |
| [Adjustometer](src/Adjustometer/README.pl.md) | Pomiar położenia nastawnika VP37 przekazywany do ECU przez I²C. |
| [Fiesta_clock](src/Fiesta_clock/) | Publikacja czasu RTC przez CAN i lokalny wyświetlacz. |
| [VP37TestBench](src/VP37TestBench/) | Współdzielony sterownik VP37 na osobnym stanowisku pompy. |
| [SerialConfigurator](src/SerialConfigurator/README.pl.md) | GUI i CLI dla Linuksa: wykrywanie USB, ustawienia i testy stanowiskowe ECU; GUI umożliwia też wgrywanie firmware. |

## Pierwsze kroki

```bash
git clone --recurse-submodules https://github.com/jaszczurtd/Fiesta.git
cd Fiesta
```

Przed uruchomieniem `runmefirst.sh` przeczytaj [instrukcję przygotowania
środowiska](doc/BUILDING.pl.md): skrypt instaluje zależności i usuwa katalogi
kompilacji modułów. Po aktualizacji istniejącego projektu uruchom
`./scripts/init_hal_submodule.sh`, aby wybrać commit HAL zapisany w Fieście.
Skrypt zatrzyma się, jeśli HAL zawiera lokalne zmiany.

## Dokumentacja

- [Architektura i komunikacja](ARCHITECTURE.pl.md)
- [Kompilacja, wgrywanie i debugowanie](doc/BUILDING.pl.md)
- [Testy hostowe i CI](doc/TESTING.pl.md)
- [Aktualizacja JaszczurHAL](DEPENDENCIES.pl.md)
- [Protokół Serial Configuratora](src/common/scDefinitions/PROTOCOL.pl.md)
- [Zakres MISRA i zasady przeglądu](MISRA.pl.md)
- [Codzienna kompilacja przez systemd](src/ECU/scripts/systemd/README.pl.md)
- [Galeria i zdjęcia stanowiska](doc/GALLERY.pl.md)

## Zakres walidacji

Projekt nie deklaruje zgodności z ISO 26262, AUTOSAR ani formalnej zgodności
MISRA. Analiza MISRA obejmuje ECU, współdzielony kod VP37 i Adjustometer;
dla dwóch ostatnich bramka odrzuca aktywne znaleziska poza zapisanymi
odstępstwami. Testy hostowe używają mocków HAL i nie potwierdzają zachowania
elektrycznego ani mechanicznego. Projekt ma stanowisko silnikowe, ale nie ma
automatycznego stanowiska HIL.

RP2040 nie ma kwalifikacji AEC-Q100. Pomiar obrotów silnika używa przerwań
GPIO, a Adjustometer mierzy okresy przez PIO/DMA. Zmiany sterowania wymagają
prób sprzętowych. Wyniki testów i CI dotyczą sprawdzanego przez nie zakresu;
nie zastępują przeglądu ani walidacji całego pojazdu.
