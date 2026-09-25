# De verificat de Andrei — PirateSeas

Lucrurile de mai jos nu le pot confirma singur. Fiecare are ce te uiți, cum
arată bine și de ce nu pot eu.

## Prima pagină — citește doar asta (25.09)

Lista are 38 de puncte și de şase zile n-a fost atinsă. Nu-ţi cer să o parcurgi.
**Mai jos sunt singurele lucruri care contează acum**, în ordinea în care
contează; restul e arhivă, etichetată ca atare la sfârşitul paginii.

### O decizie care e a ta

**Banda de avarie a tunurilor — punctul 38.** Acum, 12 din 41 de lovituri în
cocă scoteau un tun din afet, **toate 12 de sub punte** — şi după ce coca a
primit coliziunea ei pe 25.09, tot **7 din 37**, tot de sub punte. Panoul
GUNS îţi arată atunci un afet distrus de o ghiulea care a intrat la linia de
plutire. Recomandarea mea e să mut banda unde sunt tunurile; asta face
dărâmarea unui tun de vreo **douăsprezece ori mai rară**, şi de aceea nu o fac
fără tine. Detaliile şi cele trei variante sunt la punctul 38.

### O întrebare veche, încă deschisă

**Căpitanul AI îşi rezolvă singur distanţa şi avansul; tu le judeci din ochi.**
E o decizie, nu o scăpare (README, „Cum ocheşti"): cine îşi aliniază singur
lovitura are dreptul să rateze. Dar înseamnă că tu şi ea nu jucaţi acelaşi joc.
Am măsurat-o într-o sesiune anterioară şi **n-am scris cifra în proiect**, deci
n-o repet aici ca fapt; dacă vrei să hotărăşti pe ea, o re-măsor întâi.

### Cinci lucruri de văzut, în ordinea asta

1. **37 — aşchiile de la o lovitură.** Reparat pe 25.09: coca are coliziunea
   ei, ghiuleaua se opreşte pe lemn (0,00 m, măsurat pe 37 de lovituri). Ce nu
   pot vedea eu: dacă aşchiile ies din SCÂNDURĂ, la înălţimea la care a intrat.
2. **36 — focul de la gura tunului.** `0,10 s` şi `60 000 cd/m²` sunt numere pe
   care le-am ales singur.
3. **35 — panoul GUNS cu trei stări.** Două pătrăţele palide la 0,30 şi 0,12
   alpha; mă aştept să fie prea apropiate. (Dacă la decizia de mai sus alegi să
   mut banda, starea „afet distrus" o vei vedea rar.)
4. **33 — ochirea: te obligă să manevrezi, sau doar te enervează?**
5. **34 — proporţia navei** după ce a fost ridicată pe linia ei de plutire.

### Restul

Punctele 1–15 sunt de la începutul proiectului; cele marcate REZOLVAT sau
CONFIRMAT nu cer nimic. Punctele 16–32 sunt judecăţi de ochi care rămân
valabile, dar niciuna nu blochează nimic: citeşte-le când ai chef, nu înainte de
cele de mai sus. **31 e deja răspunsă** (magazia finită, 16.09).

---

## 1. REZOLVAT 12.09 — apa se vede

Nu mai e de verificat de tine. Suprafața plugin-ului Water nu desena nimic și
n-am reușit s-o conving: toate porțile raportau sănătos, iar aceeași cadră cu
randarea apei pornită și oprită ieșea identică. Am ocolit-o. Marea e acum
desenată de proiect, dintr-o grilă proprie, cu ACELEAȘI șase unde pe care le
simte plutirea: coca e tăiată la linia de plutire, valurile se mișcă.

Plutirea folosește în continuare plugin-ul. Doar imaginea e a noastră.

<details><summary>Ce era înainte, păstrat pentru context</summary>

### Suprafața oceanului lângă nave, problema veche

**Starea măsurată:** cerul, norii, atmosfera și navele se randează corect. Apa
**nu** taie coca. Am pus două nave statice de referință exact la Z=0 și Z=-300
lângă cea a jucătorului: toate trei își arată chila întreagă. Deci nu e o
problemă de înălțime de spawn, ci suprafața apei nu se desenează în zona
navelor. Departe, spre orizont, se vede o suprafață albăstruie, probabil
far-mesh-ul de apă sau atmosfera.

**Ce te uiți:** Deschizi proiectul în editor, apeși Play, te uiți dacă apa taie
coca la linia de plutire.

**Cum arată bine:** coca intră în apă, chila nu se vede.

**Dacă tot lipsește:** în editor selectează actorul `WaterZone`, mișcă-l un pic
și pune-l înapoi la (0,0,0). Plugin-ul Water reconstruiește quad tree-ul și
textura de informații la editarea actorului. Asta e exact pasul pe care nu-l pot
declanșa dintr-un proces fără interfață.

**De ce nu puteam:** reconstrucția apei se face pe un eveniment de editor
(`PostEditChange`), care nu e expus în Python și nu se declanșează într-un
commandlet.

</details>

---

## 2. REZOLVAT — nava plutește, măsurat

Nu mai e de verificat de tine. Din 10.09.2026 nava are plutire fizică reală și
am măsurat-o dintr-o rulare fără interfață. Pawn-ul scrie o linie pe secundă:

```
z=15.6 → -96.0 → -65.5 → se stabilizează la -63.4
roll: -2.1 → -0.0     inWater=1  queryOk=1  surfaceZ=0.0
```

Se leagănă, se redresează singură și se oprește cu 63 cm sub linia de plutire
proiectată. Interogarea suprafeței apei răspunde corect.

**Ce rămâne de văzut cu ochiul tău:** dacă apa taie coca vizual, adică punctul 1.
Fizica știe unde e apa, dar suprafața tot nu se desenează.

## 2b. Nava stă corect pe linia de plutire — CONFIRMAT DE MINE 12.09

De când apa se desenează pot să mă uit singur: coca intră în apă cam la o
treime de jos, chila nu se vede, puntea e clar deasupra. Îți rămâne doar să
spui dacă ți se pare corect ca senzație.

<details><summary>Cum era formulat înainte</summary>

**Ce te uiți:** În Play, uită-te la cocă unde intră în apă.

**Cum arată bine:** Apa taie coca cam la o treime de jos. Chila nu se vede.
Puntea e clar deasupra apei.

**Cum arată prost:** Nava plutește în aer deasupra apei, sau e scufundată până
la parapet.

**De ce nu puteam:** fără suprafață desenată nu exista linie de plutire de
privit.

</details>

---

## 3b. A și D chiar rotesc nava acum — REPARAT 12.09

**Ce te uiți:** Cu pânza sus, apeși A, apoi D. Apoi aceleași lucruri cu
săgețile stânga/dreapta.

**Cum arată bine:** Toate patru rotesc nava, la fel.

**De ce ți-o cer:** A și D n-au rotit nava NICIODATĂ. Două axe de input erau
legate la aceeași funcție, iar cea a săgeților, legată a doua, scria zero peste
valoarea tastelor în fiecare cadru. Le-am separat și acum se adună, dar input-ul
de tastatură nu există într-o rulare fără interfață, deci confirmarea e a ta.

## 3. Comenzile mișcă nava

**Ce te uiți:** Apeși W şi S, apoi A și D, apoi miști mouse-ul.

**Cum arată bine:** W şi S STRÂNG şi LASĂ pânza — bara SAIL SET din colţ se mută,
şi abia după aia se mută nava, cu întârziere de navă grea. A şi D pun CÂRMA, iar
nava începe să gireze doar dacă are drum; cu pânza strânsă, cârma nu face nimic.
Mouse-ul mişcă DOAR camera; coca rămâne pe drumul ei.

**Cum arată prost:** Nu se mișcă nimic (maparea de input n-a fost citită). Sau
nava se roteşte după mouse. Sau W o accelerează instantaneu, ca pe o maşină.

**De ce nu pot eu:** Input-ul se leagă la runtime, într-o sesiune de joc cu
tastatură. Într-un proces fără interfață nu există input de citit.

**Corectat:** punctul ăsta descria până acum un throttle şi o cocă rotită din
mouse — comenzile navei-jucărie din prima săptămână, înlocuite de mult de pânză
şi cârmă. Îţi cerea să confirmi ceva ce jocul nu face.

---

## 4. Camera e la distanța potrivită

**Ce te uiți:** Cât din navă și din mare vezi în Play.

**Cum arată bine:** Vezi toată nava, cu ceva mare în jur. Braţul camerei e la
4200 uu, adică 42 m în spate, cu întârziere lină la mișcare.

**Cum arată prost:** Camera e în interiorul velelor, sau atât de departe încât
nava e un punct.

**De ce nu pot eu:** E o judecată vizuală de senzație, nu o măsurătoare.

---

## 5. Materialele navei arată a lemn

**Ce te uiți:** Culorile coca / punte / vele în Play.

**Cum arată bine:** Cocă maro foarte închis, punte mai deschisă și caldă, vele
crem, tunuri negre metalice.

**Cum arată prost:** Totul gri, sau un caroiaj gri-alb pe vreo piesă. Caroiajul
înseamnă că motorul a înlocuit materialul cu cel implicit şi a spus-o într-o
linie de log care nu conţine cuvintele „failed to compile" — a costat o sesiune
o dată deja.

**De ce nu pot eu:** Am confirmat din log că toate cele opt instanţe se creează
şi se leagă de sloturile corecte, dar nu pot spune dacă la ochi arată a lemn.

**Corectat:** punctul ăsta zicea „şase materiale" şi punea vina pe importul FBX
dacă totul iese gri. Sunt opt instanţe (şapte pe navă plus una de vegetaţie), şi
culorile nu mai vin din Blender de mult: vin din texturile sintetizate cu numpy
plus o nuanţă pe piesă. FBX-ul nu mai aduce nicio culoare.

---

## 6. REZOLVAT 10.09 — lanţul de compilare

Punctul ăsta te întreba dacă vrei să instalezi Visual Studio Build Tools, fiindcă
fără compilator C++ nu se puteau scrie tunuri, avarii, scufundare şi nave
inamice. Ai instalat VS 2026; modulul C++ compilează din 10.09, iar tot ce
depindea de decizia asta e livrat de săptămâni.

Rămâne aici doar ca să nu se renumeroteze lista. Nu ai ce verifica.

---

## 7. Scufundarea

**Ce te uiți:** În Play, deschizi consola cu `~` și scrii `Scuttle`. Sau te
lași lovit de 17 ori.

**Cum arată bine:** Nava se înclină spre tribord și se afundă cu prova, în
vreo 12 secunde puntea ajunge la nivelul apei, stă așa ~5 secunde cu
înclinarea tot mai mare, apoi alunecă sub apă. Camera o urmează. După 35 de
secunde de la lovitura fatală ești pe o navă nouă, la punctul de start, iar
inamicul se întoarce spre ea.

**Cum arată prost:** Nava dispare brusc, sau rămâne la suprafață înclinată,
sau nava nouă apare peste epavă.

**Un lucru pe care nu l-am schimbat:** camera e prinsă de cocă, deci coboară
cu epava și în ultimele secunde ajunge sub apă. Mi se pare corect (te duci la
fund cu nava ta), dar dacă ți se pare confuz, spune-mi și o desprind ca să
rămână deasupra, privind în jos după epavă.

**De ce nu pot eu:** Am măsurat toată secvența din log (adâncime, înclinare,
portanță, secundă cu secundă, pe ambele borduri), dar fără randare nu văd cum
arată. Atenție: apa tot nu se desenează (punctul 1), deci „sub apă" se vede
doar ca o cocă care coboară sub orizont.

---

## 8. Chila și urcarea în vânt

**Ce te uiți:** Pune prova cam la 60 de grade de vânt și ține-o acolo un minut.

**Cum arată bine:** Nava înaintează oblic și câștigă vizibil teren spre direcția
din care bate vântul. Alunecarea laterală se vede ușor, ca un mers pieziș.

**Cum arată prost:** Merge repede dar rămâne pe loc față de vânt, sau se
răsucește ciudat la cârmă.

**De ce nu pot eu:** Am măsurat polarul complet din log, viteză, derivă și
câștig în vânt la fiecare două grade de rotație a vântului. Senzația la cârmă e
însă a ta.

---

## 9. Avarii pe zone: tirul înalt contra tirului jos

**Ce te uiți:** Ține **Shift stânga** apăsat și trage cu Q sau E în inamic, de
câteva ori. Apoi uită-te ce face nava lui.

**Cum arată bine:** Tirul înalt nu-i scade deloc coca, dar îl încetinește tot
mai mult, până rămâne în loc. Tirul normal, fără Shift, îi scade coca și îl
scufundă, dar îl lasă să se miște.

**Cum arată prost:** Nu se simte nicio diferență între cele două.

**De ce nu pot eu:** Am măsurat fiecare efect separat, cu avarii impuse din
linia de comandă: viteza cu greementul la 100, 60, 30 și 0 la sută, viteza de
giraţie cu cârma la 100, 50 și 0, și numărul de ghiulele cu tunurile scoase.
Ce nu pot măsura e dacă alegerea se simte ca o alegere când ești la cârmă.

---

## 10. Instrumentele

**Ce te uiți:** Apeși Play și te uiți la roza din dreapta sus cât navighezi.
Întoarce nava încet de tot, prin toate unghiurile față de vânt.

**Cum arată bine:** Linia plină din roză se umflă și se subțiază pe măsură ce
întorci, și e cea mai lungă exact acolo unde nava merge cel mai tare. Sectorul
roșu se rotește odată cu vântul, nu cu nava. Când intri cu prova în el, pânzele
se golesc și scrie IN IRONS.

**Cum arată prost:** Roza nu se mișcă deloc, sau se mișcă invers față de cum
întorci, sau linia plină nu are legătură cu viteza pe care o simți.

**De ce nu pot eu:** Am verificat cifrele din spate dintr-o linie de log scrisă
o dată pe secundă, și am comparat capturile cu ea. Dar dacă roza se rotește
invers, asta se vede doar cu mâna pe cârmă.

**Al doilea lucru:** spune-mi dacă panoul e prea încărcat. Am pus deliberat
puține lucruri, dar n-am jucat.

---

## 11. Escadronul

**Ce te uiți:** Pornești cu `-EnemyCount=3` și te uiți cum se apropie.

**Cum arată bine:** Se pun în șir, una în urma alteia, nu îngrămădite. Nu se
ciocnesc. Când una e între tine și alta, cea din spate nu trage prin ea.

**Cum arată prost:** Se ciocnesc, sau se învârt una în jurul alteia, sau vin
toate trei în linie de front, cot la cot.

**De ce nu pot eu:** Am măsurat distanțele dintre ele din log, secundă cu
secundă, și am numărat ciocnirile și loviturile fratricide. Dar dacă linia
arată ca o linie, asta se vede cu ochiul.

**Și o întrebare de echilibru, pusă corect de data asta.** Implicit sunt două.
Cu trei, o navă complet pasivă (fără pânză, fără cârmă, fără niciun foc tras)
a fost scufundată O SINGURĂ DATĂ în 300 de secunde, la secunda 138, iar cea de
schimb a supraviețuit restul. Din cele 52 de lovituri încasate, doar 24 au
intrat în cocă; restul au fost tir înalt, care prin construcție nu scufundă pe
nimeni.

Nu pot alege între două și trei din ce-am măsurat, fiindcă n-am măsurat
niciodată un jucător care chiar joacă, și aia e singura măsurătoare care
contează aici. Încearcă-le pe amândouă cu `-EnemyCount`.

---

## 12. Insula

**Ce te uiți:** Pornești cu `-Islands=1` și navighezi până o vezi. Apoi intră în
ea dinadins, cu toate pânzele sus.

**Cum arată bine:** Un deal cu plajă, nu o cutie. Marea îl taie la linia de
plutire de jur împrejur, fără mal vertical și fără apă care urcă pe uscat.
Când te apropii prea mult, nava încetinește în apa mică și e împinsă înapoi în
larg — nu se oprește într-un zid și nu se cațără pe nimic. Strângi pânza și
iese singură.

**Cum arată prost:** Te oprești brusc, ca într-un perete invizibil. Sau te urci
pe insulă. Sau ești zvârlit înapoi mai repede decât ai intrat.

**De ce nu pot eu:** Am măsurat viteza de intrare, adâncimea în banc, viteza de
ieșire, pescajul și portanța pe toată durata, și ghiulelele oprite de stâncă.
Dar „arată a insulă sau a cutie" și „senzația de eșuare" se văd cu ochiul.

## 13. Marea de sub cocă, după reparația de scară

**Ce te uiți:** Te uiți în jos, lângă bord, pe o hulă.

**Cum arată bine:** Valuri care se mișcă sub navă, cu formă, până aproape de
cocă.

**Cum arată prost:** O farfurie plată în jurul navei, cu valuri care încep abia
departe.

**De ce nu pot eu:** Mesh-ul mării era de 100 de ori prea mare de la felia 7 —
măsurat pe asset, 600 km în loc de 6 km — deci primul inel de geometrie era la
250 m de cocă în loc de 2,5 m. L-am reparat și am verificat dimensiunea, dar
dacă detaliul de sub navă arată acum ca apă se vede doar privind.

---

## 14. Căpitanul inamic pe un mal sub vânt

**Ce te uiți:** Pornești cu
`-Islands=3 -IsleX=-35000 -IsleY=0 -IsleRadius=14000 -WindBearing=0 -EnemyX=-75000 -EnemyY=0`
și te uiți ce face nava inamică atunci când atinge bancul.

**Cum arată bine:** Se oprește în apa mică, pânza scade vizibil, apoi o pune la
loc și pleacă spre larg — în bordeie, dacă largul e în vânt. Arată ca o navă
care se smulge, nu ca una împinsă.

**Cum arată prost:** Rămâne acolo cu toate pânzele sus. Sau se învârte pe loc.
Sau iese și intră imediat la loc, la nesfârșit.

**De ce nu pot eu:** Am măsurat secundele eșuate, cursul cerut față de cursul
pe care i l-a dat rigul, murele, și pânza secundă cu secundă. Dar „arată a
manevră de marinar sau a bâjbâială" se vede doar privind.

**Și o întrebare de echilibru:** acum atinge uscatul mai des decât înainte,
fiindcă nu se uită înainte. Spune-mi dacă asta arată prostesc sau arată ca o
navă care își asumă riscuri ca să ajungă la tine.

---

## 15. Cum schimbă bordul căpitanul inamic

**Ce te uiți:** Pune-te în vânt față de el — adică fă-l să te urmărească
bordeind — și uită-te ce face când schimbă mura.

**Cum arată bine:** Când are viteză, pune cârma și trece prin ochiul vântului pe
drumul scurt, în câteva secunde. Când e aproape oprită, o ia pe drumul lung, prin
pupă.

**Cum arată prost:** Se oprește cu prova în vânt și rămâne acolo mult. Sau o ia
pe drumul lung chiar când mergea repede. Sau începe s-o ia într-un fel și se
răzgândește la jumătate.

**De ce nu pot eu:** Am măsurat salvele, loviturile, virajele, venirile în vânt,
cât stă blocată și unde termină. Dar dacă manevra arată a marinărie sau a
șovăială se vede doar privind.

**Ce știu deja și n-am ascuns:** acum se blochează cu prova în vânt până la vreo
40 de secunde, față de vreo 6 înainte. E prețul, și e plătit o singură dată pe
manevră în loc de o sută de metri pierduți la fiecare schimbare de mură. Dacă la
privit pare prea lung, se poate ridica pragul (`-AITackAbove`), cu prețul că
redevine mai timidă.


---

## 16. Arată a mare, sau a piscină?

**Ce te uiți:** Ieşi în larg şi uită-te la apă de aproape, de la nivelul punţii,
şi apoi spre zare. Întoarce nava aşa încât soarele să fie în faţa ta şi apoi în
spate.

**Cum arată bine:** Suprafaţa e spartă de riduri mărunte peste hulă, nu netedă
între creste. Cu soarele în faţă se vede un drum de sclipiri care se rupe, nu o
oglindă. Spuma apare doar pe vârfurile crestelor, rar, şi e mată — nu lucioasă.

**Cum arată prost:** Apa e o folie de plastic care se ondulează. Sau invers, e
plină de alb peste tot. Sau ridurile se văd că se repetă într-un caroiaj.

**De ce nu pot eu:** Pot măsura pragul spumei şi pot număra câţi pixeli sunt
albi. Nu pot spune dacă suprafaţa „se citeşte" ca apă în mişcare — aia e o
judecată de ochi, pe un cadru care se schimbă.

**Ce știu deja și n-am ascuns:** ridurile se văd că se repetă într-o dungă
orizontală în zare.

**Corectat:** aici scria „marea n-are siaj în spatele cocii". Are, de o sesiune
întreagă — siaj din firimituri, guler la cocă, braţe Kelvin, inele de strop şi,
de azi, berbeci pe creste. Punctul te trimitea să confirmi o lipsă care nu mai
există. Uită-te şi la spuma de pe creste cât eşti acolo: pragul ei era până azi
o fracţie dintr-un maxim pe care marea nu-l atinge niciodată, deci nu se spărgea
NIMIC; acum se sparge vreo 11% din suprafaţă şi cifra e tipărită în log.

---

## 17. Lemnul de pe cocă: scânduri sau tapet?

**Ce te uiți:** Apropie camera de bordaj (`-ShipShotCam=beam`) şi uită-te la
îmbinări, la linia de plutire şi la punte.

**Cum arată bine:** Scândurile merg de la prova la pupă, îmbinările sunt
decalate, fiecare scândură are altă nuanţă. Sub linia de plutire lemnul e mai
închis şi lucios, deasupra e mat.

**Cum arată prost:** Textura „înoată" pe cocă în timp ce nava merge. Sau
scândurile merg de sus în jos. Sau desenul e vizibil acelaşi la fiecare metru.

**De ce nu pot eu:** Am verificat că proiecţia e în spaţiu local şi că
îmbinările sunt decalate în generator. Dacă la ochi arată a lemn de navă sau a
podea laminată e altceva.

---

## 18. Cât de departe mai e până la „realist"?

**Ce te uiți:** Priveşte cadrul întreg şi spune ce te scoate primul din el.

**De ce întreb:** Ordinea în care le fac ar trebui să fie ordinea în care TE
deranjează, nu ordinea în care mi se pare mie că sunt grele.

**Lista veche e livrată.** Scria aici „cordaj, siaj, stropi, fum de tun,
vegetaţie, spumă la ţărm" — toate şase există acum (fumul merge, dar l-ai parcat
tu). Lista onestă de acum, cât o ştiu eu:

- pânza nu FÂLFÂIE: vela se îndoaie odată cu catargul, dar n-are mişcarea ei
  proprie, rapidă, când e prinsă în vânt;
- frunzele sunt solide, nu cartonaşe cu decupaj — silueta e mai groasă decât
  trebuie de aproape;
- cerul e tot cel implicit al motorului: `-Hour=` mută soarele, culoarea şi
  expunerea, dar norii şi atmosfera rămân aceiaşi, şi timpul nu curge în timpul
  unei partide;
- nu există sunet, deloc;
- coca nu poartă urme de lovitură: gaura se vede în cifre, nu pe lemn;
- nu există echipaj, nici interior.

Care dintre astea te scoate prima din cadru?


---

## 19. Greementul: arată a navă sau a păienjeniş?

**Ce te uiți:** Apropie camera de catarge (`-ShipShotCam=rig`) şi uită-te unde
se leagă sarturile jos, cât de sus urcă scările, şi dacă straiurile trec prin
vele.

**Cum arată bine:** Sarturile pleacă din parapet, în afara bordului, şi se string
spre cruce. Scările se opresc pe la verga de jos. Braţele pleacă de la capetele
vergilor spre pupă. Nimic nu pluteşte legat de nimic.

**Cum arată prost:** Funii care încep în aer. Scări care urcă peste velele de
sus. Straiuri atât de groase încât par bârne. Sau, invers, atât de dese încât
nu se mai vede nava.

**De ce nu pot eu:** Pot verifica din cod că fiecare funie pleacă dintr-un punct
calculat de pe cocă şi ajunge într-un punct calculat de pe catarg. Dacă
ansamblul arată a marinărie sau a desiş e o judecată de ochi.

**Ce știu deja și n-am ascuns:** cordajul SCÂNTEIA de departe — funii de trei
centimetri la câteva sute de metri sunt sub un pixel. Are LOD acum: peste
`RopeFadeStartCm` (240 m) vârfurile sunt trase spre origine până când funia e un
punct. Ce merită ochiul tău s-a mutat: dacă dispariţia se vede ca o smucitură la
240 m, şi dacă se subţiază DOAR parâmele — orice altceva care se micşorează la
distanţă înseamnă că butonul a scăpat pe o piesă care nu e cordaj.


---

## 20. Fumul de tun: merită să fie pornit implicit?

**Ce te uiți:** Dă o salvă de aproape
(`-ShipFireTest=8`), apoi priveşte de la travers şi de la pupă. Uită-te şi la o
escadrilă întreagă care trage, nu doar la o navă.

**Cum arată bine:** La gura tunului se naşte o vălătucire care stă o clipă pe
loc, apoi se ridică, se subţiază şi pleacă sub vânt, rămânând în urmă când nava
merge. Patru tunuri fac un banc de-a lungul bordului, nu o minge.

**Cum arată prost:** O pată care călătoreşte cu nava. Sau plăci plate care se
văd că sunt patrulatere. Sau atât de deasă încât nu mai vezi ţinta.

**De ce nu pot eu:** Pot măsura că nu atinge tirul (56 de linii şi 11 lovituri,
identic cu fumul stins) şi pot verifica fiecare număr din mişcare. Dacă e
frumos, şi dacă e prea mult sau prea puţin într-o luptă adevărată, se vede doar
privind.

**Ce știu deja și n-am ascuns:** e mai închis la culoare decât fumul de pulbere
alb-cenuşiu din tablouri — pentru că e privit contra cerului, ceea ce chiar îl
face întunecat, dar poate părea prea funinginos. **APRINS implicit din 17364af** — întrebarea din titlul punctului a primit
răspuns, iar linia de bază a fost reînregistrată deliberat în acelaşi commit.
`-ShipSmoke=0` cumpără lumea veche înapoi. (Randul ăsta a spus „E stins
implicit” încă două commit-uri după ce fusese aprins.)


---

## 21. Insula: arată locuită sau decorată?

**Ce te uiți:** Treci pe lângă insulă de la vreo două sute de metri, apoi apropie-te
până la plajă. Uită-te la linia cerului şi la unde se opreşte verdele.

**Cum arată bine:** Silueta dealului e ruptă de palmieri, nu netedă. Palmierii se
apleacă, şi se apleacă mai mult sau mai puţin, nu toţi la fel. Nimic nu creşte pe
nisip şi nimic nu atârnă pe o faţă prea abruptă. De aproape se văd tufe între ei,
nu doar palmieri.

**Cum arată prost:** Copaci în rânduri sau într-un caroiaj. Ceva care creşte în
apă sau pe plajă. Toţi palmierii identici, sau toţi verticali. Sau atât de deşi
încât dealul dispare sub ei.

**De ce nu pot eu:** Pot verifica din log că fiecare plantă a fost aşezată pe un
punct trasat de pe mesh, că pragurile vin din material şi nu din constante
retastate, şi câte au fost respinse şi de ce. Dacă dealul arată *locuit* sau doar
*decorat* e o judecată de ochi.

**Ce știu deja și n-am ascuns:** e prima treaptă, deliberat — frunzele sunt
geometrie solidă, nu cartonaşe cu decupaj. Se mişcă în vânt de azi — vezi 23. Şi
aproape nimic de pe deal nu e destul de abrupt cât să treacă pragul de rocă: în
scenariul de eşuare, `too steep=1` din 258 de puncte încercate. Practic piatra
din material nu se arată aproape niciodată pe insula asta.

## 22. Relieful lemnului: iese în afară sau e desen pe pânză?

**Ce te uiți:** Apropie-te de navă şi uită-te pe rând la cocă, la punte, la
catarge şi la o parâmă, cu soarele o dată în faţă şi o dată în spate. Apoi la
pânze, tot aşa.

**Cum arată bine:** Scândurile au umbră între ele, şi umbra se mută când se mută
nava faţă de soare. Catargul pare rotund, nu un tub plat. Nimic nu pare luminat
dintr-o direcţie în care nu bate soarele, şi nicio suprafaţă nu e neagră când
vecina ei, întoarsă la fel, e luminată.

**Cum arată prost:** Lemn plat, ca o poză lipită. Sau invers: o faţă neagră
lângă una luminată, pe aceeaşi piesă. Catarge negre pe partea dinspre soare.

**De ce nu pot eu:** Am schimbat BAZA în care se aplică harta de relief. Niciun
mesh din proiect n-are UV-uri, deci nava n-avea cadru tangent — motorul primea o
hartă tangenţială şi o aplica într-o bază pe care n-o definise nimeni. Acum
relieful înclină normala geometrică într-un cadru construit pe loc din ea. Pot
dovedi cu capturi că prima încercare (normala înlocuită cu una luată din planul
proiecţiei) înnegrea catargele şi că asta nu se mai întâmplă; dar dacă lemnul
*arată* a lemn cu relief sau a tapet e o judecată de ochi.

**Ce știu deja și n-am ascuns:** direcţia fibrei din relief nu e aliniată cu
direcţia scândurilor din culoare — cadrul se construieşte dintr-un vector fix,
nu din axele proiecţiei, fiindcă alinierea la axe se degenerează exact pe cocă
şi pe punte. La zgomotul fin al texturilor astea nu se vede; pe o textură cu
dungi clare s-ar vedea.

---

## 23. Vântul în plante şi în greement: viu, sau gelatină?

**Ce te uiți:** Stai lângă insulă cu vânt mare (`-WindSpeed=16`) şi uită-te un
minut la palmieri, apoi la propriul greement de aproape (`-ShipShotCam=rig`).
Apoi acelaşi lucru pe vânt mic (`-WindSpeed=5`).

**Cum arată bine:** Palmierii stau ÎNCLINAŢI în bătaia vântului şi se leagănă în
jurul înclinării, nu se plimbă dintr-o parte în alta pe lângă verticală. Baza nu
se mişcă deloc; vârful se mişcă cel mai mult. Nu bat toţi la fel, în acelaşi
ritm. Pe vânt mic abia se simte. Greementul şi catargele se îndoaie ÎMPREUNĂ —
un sart rămâne prins de catargul lui.

**Cum arată prost:** Palmieri care se unduiesc ca sub apă, sau care se mişcă din
rădăcină. O întreagă costişă care bate la unison, ca un metronom. Sarturi care se
desprind de catarg. Sau, în cealaltă direcţie: absolut nimic, pe orice vânt.

**De ce nu pot eu:** Pot măsura că mecanismul funcţionează — la un moment dat,
1364 de pixeli de pe deal sunt în altă parte decât cu plantele îngheţate, şi 75%
dintre ei se mai mută încă o dată în jumătate de secundă. Ce NU pot măsura e
dacă arată a vânt: diferenţa de imagine cadru-cu-cadru la distanţa aia e
dominată de umbrele norilor, iar netezirea temporală (TAA) chiar face zonele în
mişcare să pară mai puţin schimbătoare. Am încercat trei instrumente înainte să
recunosc asta.

**Ce știu deja și n-am ascuns:** amplitudinea e aleasă FIZIC, nu ca să iasă bine
la o măsurătoare — la 14 m/s vârful unui palmier de şase metri parcurge vreo
optzeci de centimetri. De la două sute de metri ăsta e un pixel sau doi, şi aşa
şi trebuie: un palmier care se vede clar mişcând de la distanţa aia ar fi un
palmier care se mişcă greşit. A fost o versiune cu 2,2 metri — un ştergător de
parbriz.

---

## 24. Ora din zi: e apus, sau doar portocaliu?

**Ce te uiți:** Aceeaşi scenă la `-Hour=6.5`, `-Hour=12` şi `-Hour=17.5`. Uită-te
la unde cad umbrele, la culoarea lemnului şi la cât vezi în umbră.

**Cum arată bine:** La 6:30 şi la 17:30 lumina rade pe apă şi umbrele greementului
se întind lung PE MARE, în direcţii opuse între cele două. Lemnul e cald, nu
spălat. Se vede în umbră: nava nu e siluetă. La prânz umbrele sunt scurte şi sub
cocă, iar totul e mai plat — aşa şi trebuie.

**Cum arată prost:** Apus portocaliu în care nava e o siluetă neagră (banda de
expunere n-a coborât cu lumina). Sau invers, un prânz spălat. Sau umbre care cad
în aceeaşi direcţie la 6:30 şi la 17:30.

**De ce nu pot eu:** Pot măsura unde e soarele, câţi lux dă, la ce temperatură de
culoare şi ce bandă de expunere a primit — toate sunt în `SKYLOG` şi în linia de
bază. Dacă un apus ARATĂ a apus e altceva.

**Ce știu deja și n-am ascuns:** norii şi atmosfera sunt cele implicite ale
motorului şi nu se schimbă cu ora — doar soarele, culoarea lui, lumina cerului şi
expunerea. Şi timpul nu curge în timpul unei partide: ora se alege la pornire.
Prima versiune a benzii de expunere chiar a produs silueta de la „cum arată
prost"; se vede în DEVLOG de ce.

## 25. Convoiul: se vede că un negustor a coborât pavilionul?

**Ce te uiți:** `-Convoy=2 -EnemyCount=0`, vântul liber. Tu eşti raider-ul.
Convoiul iese la ~1,9 km spre nord-est şi fuge de-a curmezişul vântului. Pune-te
în vântul lui, coboară pe el şi trage-i în greement (Shift = tir înalt).

**Cum arată bine:** După una-două salve în velatură, negustorul strânge pânza,
rămâne cu cârma la mijloc şi se leagănă în valuri, iar rândul CONVOY din panou
trece de la „0 of 2 stopped, 0 through, need 1" la „CONVOY TAKEN", verde. Al
doilea negustor îşi vede de drum. Dacă mai tragi în cel oprit, îl scufunzi;
rândul rămâne TAKEN, dar ai pierdut marfa (deocamdată doar în log).

**Cum arată prost:** Negustorul navighează mai departe cu pânza sus după ce
logul spune STRUCK. Sau rândul CONVOY lipseşte din panou. Sau negustorii ies
unul peste altul. Sau, cu vântul liber, drumul convoiului cade în zona moartă
şi unul rămâne în irons cu prova în vânt.

**De ce nu pot eu:** Pot măsura că a coborât pavilionul (`SHIPLOG … STRUCK`),
că raider-ul AI o lasă în pace din clipa aia (`AILOG target lost`) şi că misiunea
se închide o singură dată. Dacă o navă oprită ARATĂ oprită e o judecată de ochi.

**Ce știu deja și n-am ascuns:** rada e un punct pe apă, nu un port — nu e
nimic de văzut acolo. Negustorul nu are mecanica de viraj prin vânt (nu-i
trebuie pe drumul pe care îl aşez eu, dar vântul liber al jocului nu e pinat).
Panoul poate depăşi cu un rând dreptunghiul de fundal când apar şi escadronul,
şi convoiul. Şi în rulările măsurate raider-ul e o navă AI, nu tu: că un om
poate face ce face căpitanul AI e o presupunere rezonabilă, nu o măsurătoare.

## 26. Echipajul: se vede când trimiți oamenii la reparații?

**Ce te uiți:** `-ShipRigDamage=0.5 -EnemyCount=0`. Bara `HANDS` din CONDITION
și barele `FORE` / `MAIN`. Apasă R o dată, apoi încă o dată, apoi a treia oară.

**Cum arată bine:** La prima apăsare textul de lângă `HANDS` trece de la „60/60
all at the guns (R)" la „60/60 15 repairing (R)", la a doua la 30, la a treia
înapoi la toți la tunuri. Cât timp ai oameni sus, barele `FORE` și `MAIN` urcă
vizibil (cu 30 de oameni, de la jumătate la 0,85 în vreo două minute) și se
opresc la 0,85, nu la plin. Într-o luptă în care ai pierdut peste doisprezece
oameni, reîncărcarea din panoul tunurilor durează vizibil mai mult de 12 s.

**Cum arată prost:** Barele nu urcă, sau urcă până la 1,00. Textul `HANDS` nu se
schimbă la R. Reîncărcarea rămâne 12 s cu jumătate din oameni morți. Panoul
CONDITION se suprapune cu cel de dedesubt (l-am lungit cu un rând).

**De ce nu pot eu:** Pot măsura tot ce e în `CREWLOG` — câți au murit, câți
repară, cât s-a refăcut, factorul de reîncărcare — și le-am măsurat. Dacă R se
simte ca o decizie în mijlocul luptei, dacă un sfert / jumătate sunt trepte
bune, dacă barele care urcă se citesc, sunt judecăți de joc.

**Ce știu deja și n-am ascuns:** manevra velelor nu e stație (oamenii de la
reparații nu încetinesc întinsul pânzei). Cifrele de pierderi sunt alese, nu
măsurate din nimic real: două pe cocă, trei pe tun, unul sus. Negustorul nu
repară deloc. Și rata de reparație e aleasă ca un catarg să-și revină într-o
sută de secunde — un număr de joc, nu de istorie.

## 27. Prada: se simte că merită să tragi în greement?

**Ce te uiți:** `-Convoy=2 -EnemyCount=0`. Prinzi un negustor de două ori, cu
aceleaşi flag-uri: o dată ţinând Shift (tir înalt) tot timpul, o dată fără el
deloc. Uită-te la rândul PURSE din panou şi la cât ţi-a luat.

**Cum arată bine:** Cu tirul înalt, pavilionul coboară mai devreme şi punga
arată 1200. Cu tirul în cocă durează vizibil mai mult şi punga arată vreo 700.
Diferenţa se vede fără să numeri: e aproape dublu.

**Cum arată prost:** Cele două feluri de a trage plătesc la fel. Sau tirul înalt
plăteşte mai puţin. Sau rândul PURSE nu apare. Sau se suprapune peste ce e sub
el (am mai adăugat un rând sub CONVOY).

**De ce nu pot eu:** Pot măsura că perechea diferă şi cu cât (1200 contra 696,
70,9 s contra 83,1 s) şi am măsurat-o. Dacă diferenţa se SIMTE ca o alegere în
timp ce tragi, sau dacă e doar o cifră care se schimbă după, e judecată de joc.

**Ce știu deja și n-am ascuns:** punga nu cumpără nimic — nu există port,
progresie sau salvare, şi nu trece dintr-o rulare în alta. Prada nu e luată în
stăpânire: nu trimiţi oameni la bord şi nu o duci nicăieri, doar se contabilizează
în clipa în care coboară pavilionul. Iar în rulările măsurate cel care ocheşte e
un căpitan AI cu `-AIAimHigh=`, nu tu: că un om poate trage constant sus e o
presupunere rezonabilă, nu o măsurătoare.

## 28. Stăpânirea prăzii: se simte că doisprezece oameni lipsesc?

**Ce te uiți:** `-Convoy=2 -EnemyCount=0`. Opreşti un negustor (tir înalt), apoi
te apropii de el sub 150 m şi stai lângă el. Uită-te la rândul PURSE şi la
bara HANDS, apoi la cât durează reîncărcarea următoarei salve.

**Cum arată bine:** După vreo douăzeci de secunde lângă el, rândul PURSE trece
la „1 manned, 12 hands away", bara HANDS scade de la 60 la 48, iar prada rămâne
cu pânza strânsă. Reîncărcarea rămâne la fel (48 e exact câţi trebuie la tunuri).
Iei şi al doilea negustor: 36 de oameni, şi **acum** reîncărcarea se lungeşte
vizibil.

**Cum arată prost:** Nu se întâmplă nimic oricât stai lângă ea. Sau oamenii pleacă
instantaneu, fără cele douăzeci de secunde. Sau bara HANDS scade dar reîncărcarea
nu se schimbă niciodată. Sau rândul PURSE se suprapune cu ce e sub el.

**De ce nu pot eu:** Pot măsura tot — câţi au plecat, când, de la ce distanţă,
cât a scăzut factorul de reîncărcare — şi am măsurat. Dacă „a doua pradă costă"
se SIMTE ca o decizie când eşti în mijlocul convoiului, nu pot şti.

**Ce știu deja și n-am ascuns:** prada rămâne acolo unde a rămas — nu o duci
nicăieri, fiindcă nu există port. Oamenii plecaţi nu se mai întorc niciodată,
nici la sfârşitul misiunii. Cele douăzeci de secunde se CUMULEAZĂ (poţi pleca şi
reveni). Şi în rulările măsurate cel care ia prada e un căpitan AI cu
`-AIPrize=1`; cu doctrina stinsă, cel mai aproape ajunge natural de o navă
oprită e 272 m, deci un jucător trebuie să vrea să se apropie.

## 29. Portul: se vede că prada pleacă singură acasă?

**Ce te uiți:** `-Convoy=2 -EnemyCount=0 -Port=1`. Opreşti un negustor, te
apropii şi trimiţi oameni la bord. Apoi uită-te ce face nava aia, şi la cele
două rânduri din panou: PURSE şi LANDED.

**Cum arată bine:** După ce oamenii tăi urcă la bord, prada **întinde pânza
singură** şi pleacă cu vântul din pupă spre radă — nu stă pe loc. Cât e pe drum,
PURSE arată 1200 şi LANDED arată 0. Când intră în radă, LANDED urcă la 1200,
bara HANDS de la tine creşte înapoi cu doisprezece, iar textul „12 hands away"
dispare.

**Cum arată prost:** Prada rămâne pe loc cu pânza strânsă. Sau porneşte şi merge
în vânt, chinuit, în loc să fugă sub vânt. Sau LANDED urcă fără ca ea să fi
ajuns. Sau oamenii nu se întorc niciodată. Sau cele două rânduri se suprapun
peste ce e sub ele.

**De ce nu pot eu:** Pot măsura tot — când a plecat, cu ce viteză, când a intrat,
câţi oameni s-au întors, cât a ajuns la chei — şi am măsurat. Dacă „prada mea
pleacă acasă" se CITEŞTE ca atare pe ecran, sau dacă drumul ei arată ca o navă
condusă de doisprezece oameni, sunt judecăţi de ochi.

**Ce știu deja și n-am ascuns:** rada e un cerc pe apă, nu un oraş — n-ai ce
vedea acolo. Nimeni nu încearcă să-ţi recaptureze prada pe drum. Punga tot nu
cumpără nimic. Şi din două prăzi luate, în cinci sute de secunde doar una
ajunge — a doua rămâne pe mare când se termină partida.

## 30. Refitul: se simte ca banii cumpara ceva?

**Ce te uiti:** `-Convoy=2 -EnemyCount=0 -Port=1 -ShipHullTest=600`. Pornesti cu
coca la 60%. Iei o prada, o lasi sa ajunga in rada, apoi intri si tu in rada si
stai acolo. Uita-te la randurile PURSE / LANDED / COFFERS si la bara HULL.

**Cum arata bine:** Cat prada e pe drum, LANDED e 0 si COFFERS e 0 - n-ai ce
cheltui. Cand ea intra in rada, LANDED sare la 1200 si COFFERS la fel. Cand
intri TU in rada, bara HULL urca vizibil, randul COFFERS scade, si textul de
langa el spune ce ai cumparat (spent 600: 20 hands, 400 hull). Se opreste
singur cand esti intreg sau cand ai ramas fara bani.

**Cum arata prost:** Bara HULL urca fara sa fi ajuns nicio prada acasa, adica pe
gratis. Sau nu urca deloc cat stai in rada. Sau COFFERS scade fara sa se schimbe
nimic pe nava. Sau cele patru randuri se suprapun peste ce e sub ele - am
adaugat doua randuri noi in coltul ala.

**De ce nu pot eu:** Pot masura fiecare cifra - cat s-a cheltuit, cati oameni,
cata coca, in cate secunde - si le-am masurat: 20 de oameni si 400 de puncte
pentru 600, in 20 de secunde. Daca "ma intorc in port sa ma refac" se simte ca o
decizie sau ca o corvoada, nu pot sti.

**Ce stiu deja si n-am ascuns:** preturile (20 pe om, 0,5 pe punct de coca) sunt
alese ca sa aiba sens fata de o prada de 1200, nu masurate din ceva real. Punga
nu supravietuieste rularii. Ghiulelele SE cumpara (2 bucata, si sunt primul
lucru cumparat la refit); nu se cumpara tunuri sau nave. Si rada e
tot un cerc pe apa, nu un oras.

## 31. RASPUNS PRIMIT 16.09: magazia E finita implicit

Owner-ul a spus da. Patruzeci de ghiulele, zece salve, cinci pe bord. Numarul
l-am ales din suita, nu din gust: in douazeci si patru de scenarii masurate
inamicul trage 0-20 in aproape toate, 24 intr-o urmarire lunga, si 44 in duelul
de 360 de secunde - singurul loc unde patruzeci se goleste.

**Ce a ramas de verificat cu ochiul:** joaca o lupta lunga si spune-mi daca bara
SHOT si avertismentul MAGAZINE DRY iti ajung la timp - adica daca vezi ca ramai
fara inainte sa se intample, cat inca mai poti face ceva. Daca afli abia cand
apesi Q si nu pleaca nimic, bara e in locul gresit sau prea discreta.

**Ce stiu deja si n-am ascuns:** portul vinde ghiulele cu 2 bucata si le cumpara
PRIMELE la refit, deci lupta e acum legata de economie - o nava fara ghiulele si
fara bani e o nava care trebuie sa fuga. `-Shot=0` face magazia fara fund la loc,
daca vrei sa compari.

## 32. Dara ghiulelei: se vede acum unde se duc?

**Ce te uiti:** Trage cateva salve la distante diferite - 100 m, 300 m, cat tine
tunul - si uita-te la arcul pe care il lasa ghiulelele si la clipa in care cad.

**Cum arata bine:** O linie fumurie care pleaca subtire de la ghiulea si se
evazeaza in urma ei, curbata dupa traiectorie, si care e **inca in aer cand sare
stropul** - asa afli daca ai tras lung sau scurt, si cu cat. La o salva de patru
tunuri vezi patru linii distincte, nu una groasa.

**Cum arata prost:** Dispare in clipa in care ghiulea cade (atunci nu-ti spune
nimic). Sau seamana cu o sarma intinsa in loc de fum - a fost asa la prima
incercare, la 30.000 cd/m2, si arde la alb pur. Sau ascunde nava tintita.

**De ce nu pot eu:** pot masura cate segmente se lasa, ca se sting toate, ca
niciunul nu ramane desenat dupa ce i-a trecut viata (`trail_stranded` e zavorat
la zero) si ca nimic din balistica nu se misca. Daca o linie fumurie SE CITESTE
ca fum si te ajuta sa corectezi, e judecata de ochi.

**Si intrebarea pe care vreau sa mi-o raspunzi tu:** darile sunt pe TOATE
ghiulelele, inclusiv ale inamicului, cum ai cerut. Acum vezi salva care vine si
poti carmi. Spune-mi daca asta face lupta mai inteligenta sau ii ia coltii.

## 33. Ochirea: te obliga sa manevrezi, sau doar te enerveaza?

**Ce te uiti:** Intra intr-o lupta si incearca sa aduci o nava sub tunuri. Misca
mouse-ul pana tunurile se opresc si linia se face chihlimbarie, apoi foloseste
carma ca s-o aduci in arc. Apasa `X` si vezi cum se fixeaza perpendicular.
Invarte rotita si uita-te la bara de cadere.

**Cum arata bine:** Alinierea e o treaba de facut, nu un chin. Simti cand
tunurile se opresc, intelegi fara sa-ti spuna nimeni ca trebuie carma, si bara de
cadere iti spune unde ajunge lovitura inainte s-o tragi. `X` e util, nu doar
prezent.

**Cum arata prost:** Arcul de **12 grade** e prea strans si ratezi tot timpul din
motive pe care nu le poti corecta. (Daca ai incercat inainte de 19.09 si ti s-a
parut ca mouse-ul o ia razna: avea dreptate, mergea INVERS la tribord. Si bara de
cadere arata cu pana la 75 m mai putin decat adevarul. Ambele reparate - merita
reincercat pe curat.) Sau linia de ochire e greu de urmarit pe
valuri. Sau rotita e prea fina si iti ia zece secunde sa schimbi distanta. Toate
trei sunt reglabile dintr-o cifra - spune-mi care si cu cat.

**Ce stiu deja si n-am ascuns:** am ales 12 grade fiindca asa ai cerut ("1"), si
e mai strans decat limita istorica reala a unui sabord (~15). Daca se joaca prost,
`MaxTraverseDeg` e o singura constanta. **Capitanul AI n-a fost atins** - el inca
trage pe poarta lui veche de 9 grade din centrul cocii, deci deocamdata regula
noua te leaga doar pe tine. Capcana cu reincarcarea pe care o semnalam aici - o salva
cu UN tun costand aceleasi 12 secunde ca una cu patru - e reparata pe 19.09:
fiecare tun isi tine ceasul lui.

## 34. Nava, dupa ce a fost ridicata pe linia ei de plutire

**Ce te uiti:** Priveste nava din lateral, in valuri, si apoi de aproape la
bordaj cand trage. Compara cu cum tii minte ca arata.

**Cum arata bine:** Un vas cu bord, nu o barja cu catarge. Se vad gurile de tun
pe bordaj, puntea sta deasupra apei, si cand se inclina la salva bordul de sub
vant coboara fara sa intre puntea in mare.

**Cum arata prost:** Acum pluteste prea SUS si arata usoara, ca o jucarie. Sau se
leagana prea mult, fiindca sferele coborate schimba si momentul de redresare.

**De ce nu pot eu:** am masurat ca originea sta la +1,9 cm fata de -78,4 si ca
gura de tun a urcat de la 19 la 297 cm. Daca proportia CITESTE bine pentru ochi -
daca arata ca o nava de 30 m si nu ca o barca marita - nu pot spune eu.

## 35. Panoul GUNS, cu trei stari

**Ce te uiti:** Intra intr-o lupta cu magazia scurta - `-Shot=3` din linia de
comanda, sau pur si simplu trage pana ramai cu cateva ghiulele - si urmareste
cele patru pipuri de pe bordul cu care tragi, in secundele de dupa salva.

**Cum arata bine:** Dupa o salva partiala, TREI pipuri se sting pe jumatate si
al patrulea ramane aprins - si raman asa vreo douasprezece secunde, dupa care
cele trei se aprind ODATA. Trei stari care se deosebesc dintr-o privire, fara sa
te uiti de doua ori: aprins, pe jumatate, si aproape sters pentru un afet scos.

(Scria aici ca cele trei "se reaprind pe rand". Nu pot: au tras in aceeasi salva,
deci au acelasi ceas. Era un criteriu de acceptare pe care codul nu-l poate
indeplini, si te-ar fi pus sa cauti un defect inexistent.)

**Cum arata prost:** Cele trei stari se confunda intre ele - mai ales "se
serveste" cu "afet scos", care inseamna lucruri opuse (unul revine, altul nu).
Sunt la 0,30 si 0,12 alpha, adica DOUA patratele palide, si ma astept sa fie
prea apropiate. Daca sunt, spune-mi si le despart pe forma, nu pe transparenta.
Sau nu se vede nimic, si panoul pare inghetat.

**De ce nu pot eu:** HUD-ul nu se randeaza deloc sub `-NullRHI`, deci pot masura
STAREA din care deseneaza pipurile - si o masor, `guns_ready_stbd` si
`guns_down_stbd` sunt in suita - dar nu pot vedea desenul. Cat de tare difera
0,12 de alpha fata de `Faint` pe ecranul tau, si daca ochiul prinde diferenta in
toiul unei lupte, e o judecata pe care doar tu o poti face.

## 36. Focul de la gura tunului, si inaltimea tevilor

**Ce te uiti:** Trage o salva si uita-te la gurile de tun in clipa aia - e o
zecime de secunda, deci uita-te la bordul cu care tragi INAINTE sa apesi. Apoi
priveste nava din lateral, fara sa tragi, si uita-te unde stau tevile pe bordaj.

**Cum arata bine:** Un foc scurt, galben-portocaliu, care infloreste o clipa la
gura fiecarui tun si e inghitit imediat de fum. Trebuie sa fie DE LA TEAVA: focul,
fumul si teava in acelasi loc. Tevile stau sub copastie, la gurile de tun, nu jos
pe bordaj langa apa.

(Randul de mai sus a fost fals cand l-am scris: reparasem inaltimea tevilor si
lasasem LATIMEA. Focul si fumul ieseau la 640 cm de axa navei, iar gura tevii e
intre 483 si 568 - adica intre 72 si 157 cm mai inauntru. O recenzie l-a prins.
Efectele au acum propriul tabel de pozitii, `GGunMuzzleY`, iar ghiuleaua ramane
la 640 fiindca trebuie sa scape de cutia de coliziune.)

**Cum arata prost:** Focul e prea lung si citeste a lampa aprinsa, nu a foc -
atunci scad `LifeSeconds` de la 0,10. Sau e prea slab si se pierde in fum, si
atunci urc `Brightness` peste 60 000. Sau, invers, ecranul se albeste tot.

**De ce nu pot eu:** pot masura ca exista (16 focuri la patru salve de cate patru
tunuri), ca nu atinge nimic din simulare (o singura cheie difera intre `gunnery`
si `flash_off`) si ca niciun foc nu supravietuieste varstei lui. Dar cat de scurt
e "scurt" si cat de tare e "tare" sunt judecati de ochi, si sunt exact cele doua
numere pe care le-am ales singur.

## 37. Aschiile de la o lovitura in cocca — REPARAT 25.09: sar de pe lemn

**Ce te uiti:** Trage de aproape intr-o nava - `-EnemyX=3000` din linia de
comanda ajuta - si uita-te la locul unde intra ghiuleaua, nu la tunul tau.

**Cum arata bine:** Un manunchi scurt de aschii de stejar sare afara din bordaj,
se rasuceste, cade si intra in apa. Trebuie sa se vada ca ai LOVIT: pana acum o
ratare arunca o coloana de apa vizibila de la trei sute de metri, iar o lovitura
nu producea nimic.

**Cum arata prost:** Aschiile sar dintr-un loc care nu e bordajul. **Asta era
capcana stiuta, si e reparata pe 25.09:** ghiuleaua se oprea pe cutia de
coliziune a navei, o cutie dreptunghiulara - masurat, lemnul era in medie la 1,7
pana la 3,4 m in spatele fetei cutiei, si pana la 7 m la capete. Acum se opreste
pe o piele inchisa a cocii, lofata din aceleasi sectiuni ca nava vizibila, cu
parapetul cu tot. Masurat dupa: 0,00 m intre punctul raportat si lemn, la fiecare
lovitura din sapte lupte. Ce ramane de vazut cu ochiul e daca aschiile sar din
SCANDURA - din bordaj, la inaltimea la care a intrat ghiuleaua.

Sau: prea multe, prea mari, prea lente. Sau raman agatate in aer in loc sa cada.

**De ce nu pot eu:** pot masura ca exista si ca sunt exact cate lovituri au fost
(`chips_spawned` = `hull_hits` in fiecare scenariu, verificat de poarta), ca nu
ating nimic din simulare, si ca niciun manunchi nu supravietuieste varstei lui.
Dar cate aschii arata a lovitura si cate arata a explozie e o judecata de ochi.

## 38. Banda de avarie a tunurilor: unde sunt tunurile, sau unde loveşte AI-ul?

**E o decizie, nu o verificare.**

**Ce se întâmplă acum, măsurat pe 25.09** cu `tools/probe_hull_hits.py`, pe cele
şapte lupte distincte din suită (41 de lovituri în cocă):

- **12 din 41 scot un tun din afet** — şi **toate 12 au lovit coca sub punte**,
  între 21 şi 152 cm deasupra apei. Puntea e pe la 210 cm, tunurile stau pe ea,
  ţevile sunt la 280. Adică fiecare tun „lovit" a fost scos de o ghiulea care a
  intrat cu 0,6 până la 1,9 m MAI JOS de el.
- Banda în care o lovitură dărâmă un tun e `[0, 240]` cm. A fost pusă când
  gurile de tun erau la **120** — comentariul din cod o spune — iar pe 18.09 le-am
  urcat la 280 ca să nu mai tragă nava de la linia de plutire. Banda n-a urmat.
- Căpitanul AI ocheşte deliberat la linia de plutire a ţintei (tirul jos), deci
  aproape toate loviturile cad în bandă — şi aproape niciuna la înălţimea
  tunurilor. Din 41, **una singură** a intrat la 253 cm, adică acolo unde un tun
  chiar poate fi lovit; pe aia banda de acum o numără drept cocă.

**Variantele:**

- **A — banda unde sunt tunurile** (200–350 cm). Pe 25.09 lovitura se
  socoteşte deja acolo unde ghiuleaua atinge lemnul (coca are coliziunea ei
  acum), şi asta singură a dus dărâmările de la 12 la **7** din 37 — toate 7 tot
  sub punte, între 68 şi 155 cm. Cu banda mutată scad la **1**. Pipurile de
  „afet distrus" vor apărea rar, dar vor însemna exact ce spun. **Recomandarea
  mea.**
- **B — cum e acum.** Pierzi un tun la aproape o lovitură din trei în cocă, din
  ghiulele care intră la linia de plutire. Presiunea rămâne; motivul e fals.
- **C — A, plus o comandă de tir „la puntea de tunuri".** Tu poţi deja ochi mai
  sus cu rotiţa; căpitanul AI ar primi o a treia ţintă pe lângă „jos" şi „sus".
  Asta ar face dărâmarea tunurilor o TACTICĂ a cuiva, nu un accident al balisticii.

**Ce se strică dacă alegem greşit:** cu A, dacă îţi plăcea presiunea de a pierde
tunuri, ea practic dispare până când cineva ocheşte mai sus. Cu B, panoul GUNS
promite ceva ce avaria nu face — şi exact pe el te-am rugat să te uiţi la 35.

**De ce nu pot eu:** o face pe una dintre mecanici de douăsprezece ori mai rară.
E o alegere de echilibru, ca magazia finită, nu o reparaţie.

**Numerele depind de o alegere a mea**, şi o spun: 200–350 cm e „de sub punte
până la vârful copastiei". O bandă mai largă ar lăsa mai multe dărâmări. Proba
se poate rula din nou cu altă bandă (`--band=180,380`); rulează luptele în motor,
deci durează câteva minute, nu secunde.

