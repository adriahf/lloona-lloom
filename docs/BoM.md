# Llista de materials · Lloona-Lloom

[← Tornar al projecte](../README.md#maquinari)

Aquest document detalla els components de maquinari necessaris per construir el controlador físic i el sistema d'il·luminació del rellotge astronòmic Lloona-Lloom.

## Resum de components

| Component | Especificació / model | Quantitat | Funció principal |
| :---- | :---- | :---- | :---- |
| **Microcontrolador** | ESP32 (ex: WROOM-32, NodeMCU) | 1 | Cervell del sistema, càlculs, WiFi i Servidor Web |
| **Tira LED** | SK6812 RGBW (amb xip Blanc dedicat) | 144 LEDs | Representar l'atmosfera i la fase lunar |
| **Font d'Alimentació** | 5V DC, ≥ 10 Amperes (50W) | 1 | Proveir l'energia necessària per als LEDs |
| **Connector DC** | Jack DC femella o bornes de cargol | 1 | Enllaçar la font d'alimentació amb el circuit |
| **Level Shifter** | 74AHCT125N o TXS0108E | 1 | Adaptar el senyal de 3.3V de l'ESP32 als 5V dels LEDs |
| **Resistència** | 330–470 Ω | 1 | Protegir el primer LED de pics a la línia de dades |
| **Condensador** | 1000µF (Mínim 6.3V, recomanat 10V-16V) | 1 | Estabilitzar la tensió i evitar reinicis / pampallugues |
| **Perfil i difusor** | Perfil d'alumini circular + tapa opal | 1 | Suport físic i difusió de la llum (efecte terminador) |
| **Cables de potència** | Gruixuts (18 AWG o 20 AWG) | Var. | Transportar els 5V i GND amb seguretat |
| **Cables de dades** | Estàndard (22 AWG o 24 AWG) | Var. | Transportar el senyal de dades (GPIO 27) |
| **Placa de Prototipat** | Perfboard petita o PCB | 1 | Soldar i organitzar l'electrònica del "Driver" |

## Detall dels components

### Processament i connectivitat

* **Microcontrolador:** ESP32 Development Board (ex: ESP32-WROOM-32, NodeMCU ESP-32S).  
  * *Funció:* Executar el motor astronòmic, connectar-se al WiFi (NTP), servir la interfície web i enviar el senyal de dades als LEDs.

### Il·luminació

* **Tira LED:** SK6812 RGBW (Important: Assegurar-se que inclou el xip Blanc, 'W').  
  * *Quantitat:* 144 LEDs físics (72 exterior + 72 interior), segons el sketch actual.  
  * *Densitat recomanada:* 60 LEDs/m o 144 LEDs/m, depenent del diàmetre del teu perfil d'alumini.  
  * *Funció:* Renderitzar l'atmosfera diürna i les fases lunars nocturnes.

### Alimentació

* **Font d'alimentació:** 5V DC, mínim 10 Amperes (50W).  
  * *Estimació original:* Amb la hipòtesi de 60 mA per LED, 144 LEDs × 60 mA = 8,64 A. Aquesta xifra és orientativa: el consum màxim real depèn de la variant SK6812 i dels canals actius. Cal contrastar el dimensionament amb la fitxa tècnica de la tira utilitzada.  
* **Connector d'alimentació:** Jack DC femella (o bornes de cargol) per connectar la font al circuit.

### Electrònica de control i protecció

Aquests components són crucials per a l'estabilitat del senyal i la protecció del maquinari.

* **Convertidor de Nivell Lògic (Level Shifter):** 1x 74AHCT125N (o un mòdul TXS0108E / bidireccional senzill).  
  * *Justificació:* L'ESP32 envia senyals de dades a 3.3V, però els SK6812 esperen senyals a 5V. Tot i que a vegades funciona directe, a la llarga o amb cables llargs produeix parpellejos (*glitches*). Això apuja el senyal de 3.3V a 5V purs.  
* **Resistència de protecció:** 1x 330Ω a 470Ω.  
  * *Ubicació:* Es col·loca en sèrie a la línia de dades (Pin 27 de l'ESP32) abans d'arribar al primer LED.  
  * *Justificació:* Prevé danys al primer LED per pics de corrent a la línia de dades i adapta la impedància.  
* **Condensador de xoc:** 1x 1000µF (6.3V o superior, recomanat 10V o 16V).  
  * *Ubicació:* Es col·loca en paral·lel a l'entrada d'alimentació (entre +5 V i GND) el més a prop possible de l'inici de la tira LED.  
  * *Justificació:* Fa de "matalàs". Quan s'encenen molts LEDs de cop, evita que la baixada sobtada de tensió reiniciï l'ESP32 o bloquegi els LEDs.

### Estructura i cablejat

* **Xassís:** Perfil d'alumini circular (mida dependent del disseny).  
* **Difusor:** Tapa translúcida de plàstic blanc/opal per al perfil d'alumini (vital per difuminar els píxels i aconseguir l'efecte "lluna" i "terminador").  
* **Cablejat de potència:** Cable de 18 AWG o 20 AWG per a les línies de 5 V i GND.  
* **Cablejat de dades:** Cable de 22–24 AWG per a la línia del GPIO 27.  
* **Placa de prototipat:** Una placa perfboard petita o una PCB dissenyada a mida per soldar l'ESP32, el level shifter, la resistència i el condensador de forma endreçada.

## Nota de muntatge

L'alimentació de la tira LED ha d'arribar directament des de la font externa; el corrent dels LEDs no ha de passar pel pin «5V» o «VIN» de l'ESP32. El controlador s'alimenta en paral·lel segons les especificacions de la placa, amb GND comú entre la font, la tira i l'ESP32.
