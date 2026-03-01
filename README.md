# **Lloona-Lloom**

Un rellotge astronòmic i simulador celeste d'alta precisió basat en ESP32 i tires LED SK6812 RGBW.

## **Motivació del projecte**

Aquest projecte està inspirat en el projecte Mood Moon, del genial Igno Mauer. Volia fer una versió personalitzada per experimentar i poder programar les meves pròpies idees, com per exemple simular la lluna tal i com la veig per la finestra. Aquest és un projecte viu, automatitzat, pensat perquè cada dia hi hagi una configuració diferent, original i única. 

## **Què és Lloona-Lloom?**

Lloona-Lloom és un sistema incrustat (*embedded*) que calcula en temps real la posició exacta del sol i la lluna per a unes coordenades geogràfiques específiques, i renderitza aquest estat al cel simulador format per 143 LEDs.  
Inclou una interfície web mòbil (*Mobile First*), actualitzacions per l'aire (OTA) i la capacitat de simular qualsevol data del passat o del futur, permetent un viatge en el temps astronòmic.

## **L'arquitectura de maquinari (HAL)**

El projecte utilitza un perfil circular d'alumini on la tira LED es divideix físicament en dues parts. Per gestionar-ho de forma neta, el programari implementa una capa d'abstracció de maquinari (HAL) que separa la física de la matemàtica:

* **Corona exterior (cel/atmosfera):** 71 LEDs (Índexs 0 a 70).  
* **Corona interior (astre/lluna):** 72 LEDs (Índexs 71 a 142\) apuntant cap al centre.

**El món lògic vs. el món físic:** Tota la matemàtica del codi treballa amb un rellotge de 360 graus. Les funcions set\_outer\_pixel i set\_inner\_pixel s'encarreguen de traduir aquests graus als LEDs reals, aplicant inversions automàtiques (ja que la corona interior està muntada en sentit antihorari).

## **El Motor astronòmic**

Per evitar l'acumulació d'errors dels càlculs bàsics basats en fraccions de temps, el cor de Lloona-Lloom implementa un subconjunt dels **algorismes astronòmics de Jean Meeus**.

### **1\. El cicle solar i l'aleatorietat determinista**

L'ESP32 obté l'hora UTC via NTP i calcula l'angle horari i la declinació del sol. Amb això s'obté l'**altitud solar**.

* Si el sol està per sobre de l'horitzó (dia), els anells es pinten de colors fluids, diferents cada dia.  
* Per garantir que els colors no ballin si es reinicia l'aparell, s'utilitza una **aleatorietat determinista**: el dia julià local actua com a llavor (seed) de la funció *random*. Un dia concret sempre tindrà la mateixa parella de colors de la paleta.

### **2\. La fase llunar i l'angle paral·làctic**

Quan el sol es pon, el sistema passa a renderitzar la lluna. S'hi calculen dos paràmetres vitals:

* **Fracció il·luminada:** Quin percentatge de la lluna rep llum solar.  
* **Angle paral·làctic (tiltAngle):** La lluna creixent no es veu igual a l'equador (forma de 'U') que a Catalunya (forma de 'D' o 'C'). Aquest angle creua la posició de la lluna amb el zenit de la latitud/longitud, indicant cap a quins graus exactes de la làmpada s'ha de dibuixar el centre de la part il·luminada. Això fa que la lluna giri físicament tal com ho fa al cel d'aquella nit.

## **Renderitzat i anti-aliasing**

En lloc d'apagar o encendre LEDs basant-nos en si estan a la dreta o a l'esquerra, Lloona-Lloom utilitza una **projecció cosinus**.  
Calcula la distància de cada LED respecte al centre de la llum i la projecta en una escala de 1.0 (llum total) a \-1.0 (ombra total). Aquest valor es compara amb el llindar de la fracció lunar. Per evitar que la lluna sembli pixelada, s'aplica una **banda de suavitzat (smoothing\_band)**. Els LEDs que cauen just a la frontera d'ombra interpolen la seva intensitat, creant un degradat suau que simula la zona del *terminador* (el pas del dia a la nit a la superfície lunar) fent servir exclusivament el LED blanc pur (W) de la tira SK6812.

## **Interfície d'usuari (Web UI)**

L'ESP32 aixeca un servidor web asíncron amb una UI empaquetada a la memòria flaix (PROGMEM). Ofereix 3 modes de funcionament:

1. **Temps real:** Càlcul automàtic basat en l'NTP. El mode de producció per defecte.  
2. **Simulació:** Permet introduir qualsevol data i hora local per fer "time travel debugging" i veure com es renderitzarà el sistema sense haver d'esperar (per exemple, per veure la propera lluna plena).  
3. **Control manual:** Permet seleccionar corones senceres o LEDs individuals i assignar-los valors RGBW precisos (via controls *stepper*) per fer proves de colorimetria.

## **Dependències i instal·lació**

Per compilar i pujar aquest codi a l'ESP32 via Arduino IDE, cal instal·lar:

* Llibreria Adafruit NeoPixel  
* Llibreria NTPClient (de Fabrice Weinberg)  
* Targeta ESP32 al gestor de plaques.

**Important:** Les dades sensibles (WiFi, contrasenya, latitud i longitud) estan extretes del codi principal i s'han de definir en un fitxer separat anomenat secrets.h a la mateixa carpeta del projecte. Aquest fitxer **no** s'ha de pujar a cap repositori públic (afegeix-lo al teu .gitignore).

### **Exemple de secrets.h**

Crea una nova pestanya a l'IDE d'Arduino, anomena-la secrets.h i enganxa-hi el següent codi amb les teves dades reals:  
// secrets.h  
\#define SECRET\_SSID "EL\_TEU\_WIFI"  
\#define SECRET\_PASS "LA\_TEVA\_CONTRASENYA"  
\#define SECRET\_DEVICE\_NAME "Lloona-Lloom"

// Coordenades de la teva ubicació (ex: Barcelona)  
\#define SECRET\_LATITUDE 41.40  
\#define SECRET\_LONGITUDE 2.18  
