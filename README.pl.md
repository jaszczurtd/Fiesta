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
| [Clocks](src/Clocks/) | Fabryczne wskaźniki, dodatkowy ekran TFT i ostrzeżenia; pomiar ciśnienia oleju, prędkości i temperatur spalin; testy wskaźników, kontrolek i buzzera. |
| [Adjustometer](src/Adjustometer/README.pl.md) | Pomiar położenia nastawnika VP37 przekazywany do ECU przez I²C. |
| [Fiesta_clock](src/Fiesta_clock/) | Publikacja czasu RTC i temperatur powietrza przez CAN oraz lokalny wyświetlacz. |
| [VP37TestBench](src/VP37TestBench/) | Współdzielony sterownik VP37 na osobnym stanowisku pompy. |
| [SerialConfigurator](src/SerialConfigurator/README.pl.md) | GUI i CLI dla Linuksa: wykrywanie USB, ustawienia oraz testy funkcjonalne ECU i Clocks; GUI umożliwia też wgrywanie firmware. |

DPF jest nieaktywną pozycją w SerialConfiguratorze. Nie ma firmware'u,
wykrywania ani obsługi wgrywania.

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
- [Przeglądarka pomiarów VP37](tools/vp37-viewer/README.pl.md)
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

RP2040 i RP2350 nie mają kwalifikacji AEC-Q100. Nic jednak nie stoi na
przeszkodzie, by stworzyć wersję ECU z chipem który takową kwalifikację posiada.
