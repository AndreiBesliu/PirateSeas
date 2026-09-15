# De verificat de Andrei — PirateSeas

Lucrurile de mai jos nu le pot confirma singur. Fiecare are ce te uiți, cum
arată bine și de ce nu pot eu.

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

- nimic nu se mişcă în vânt: palmierii şi tufele sunt geometrie rigidă;
- frunzele sunt solide, nu cartonaşe cu decupaj — silueta e mai groasă decât
  trebuie de aproape;
- cerul e cel implicit al motorului, aceeaşi oră din zi în fiecare rulare;
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

**Ce te uiți:** Rulează cu `-ShipSmoke=1` şi dă o salvă de aproape
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
face întunecat, dar poate părea prea funinginos. E stins implicit ca să nu
schimbe baza măsurătorilor; dacă îţi place, se poate face implicit.


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
geometrie solidă, nu cartonaşe cu decupaj, şi nimic nu se mişcă în vânt. Şi
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
