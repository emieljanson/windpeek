---
date: 2026-09-13
topic: project-conversion-review
focus: Code, UX, conversie en productgroei
mode: repo-grounded
---

# Windpeek: prioriteiten voor product en conversie

## Codebase Context

Brede verkenning van de huidige werkmap, inclusief bestaande niet-gecommitte wijzigingen. Geen volledige regel-voor-regel audit. Landing en configurator lokaal geopend; geen fysieke installatie of volledige browsertests uitgevoerd. Webtests: 642 geslaagd, één mismatch rond de nieuwe rij “Hourly detail”. Productiebuild slaagt, met grote JavaScriptbundels. Geen conversiedata ingezien: omzet- en conversie-effecten hieronder zijn hypotheses.

De gedeelde native/WASM-renderer en herstelbeveiligingen zijn waardevolle bestaande bouwstenen. De eerdere review in `docs/reviews/2026-09-05-codebase-simplification.md` heeft al ongebruikte code verwijderd en legt uit waarom legacy-builds en afzonderlijke forecastclients behouden zijn.

## Topic Axes

- Betrouwbaarheid en productbeloften
- Eerste bezoek en mobiele voortgang
- Conversiemeting en vertrouwen
- Onderhoud en laadsnelheid
- Nieuwe gebruikerswaarde

## Ranked Ideas

### 1. Mobiele interesse vasthouden

- Voorstel: een actieve previewknop op mobiel, gevolgd door expliciet delen/bewaren voor installatie op desktop.
- As: eerste bezoek en mobiele voortgang.
- Basis — direct: `web/src/views/LandingView.vue` toont een uitgeschakelde mobiele configuratieknop. `web/src/config/configurationUrl.js` bewaart de configuratie al in de URL.
- Waarom: de bestaande mobiele preview krijgt een vindbare ingang en een vervolgactie.
- Nadeel: lange links met meerdere spots verdienen aandacht; geen account nodig voor de eerste versie.
- Vertrouwen: 90% in bruikbaarheid, conversie-effect te meten. Complexiteit: laag–middel.

### 2. Conversie zichtbaar maken

- Voorstel: meet preview openen, spot kiezen, winkel doorklikken en installaties starten/slagen/mislukken; splits mobiel en desktop.
- As: conversiemeting en vertrouwen.
- Basis — direct: de onderzochte webbron bevat geen productfunneltracking. README beschrijft alleen een anonieme firmwareheartbeat; installer heeft Sentry-diagnostiek.
- Waarom: vaststellen waar mensen afhaken voordat campagnes en experimenten worden opgeschaald. Een winkelklik is geen bewezen aankoop; partnerverkopen apart meten indien rapportage beschikbaar is.
- Nadeel: meetdefinities en minimale gegevensverzameling vragen bewuste keuzes.
- Vertrouwen: 95% in nut van meting. Complexiteit: middel.

### 3. Belofte en eerste actie dichter bij elkaar

- Voorstel: bovenaan “Bekijk jouw spot”, gratis software en apart benodigde hardware verduidelijken; bestaande bezitters rechtstreeks naar installatie. Voeg een korte echte installatievideo en enkele echte gebruikservaringen toe.
- As: conversiemeting en vertrouwen.
- Basis — direct: de hero is klikbaar, maar de expliciete configuratieknop volgt pas na de hardwarevergelijking. Kooproute bevat een kortingsdialoog en daarna een externe winkel.
- Waarom: bezoekers kunnen eerst hun eigen resultaat ervaren en begrijpen vervolgens wat ze kopen.
- Nadeel: extra inhoud kan de rustige pagina verzwaren; test één wijziging tegelijk bij voldoende verkeer.
- Vertrouwen: 80%. Complexiteit: laag–middel.

### 4. Huidige release en batterijclaims onderbouwen

- Voorstel: herstel de testverwachting na bevestiging van de bedoelde vergelijkingsrij; test E1003 op batterij met touch, slaap, ontwaken, tien spots, netwerkuitval en herstel. Kwalificeer batterijduur totdat metingen beschikbaar zijn.
- As: betrouwbaarheid en productbeloften.
- Basis — direct: `web/tests/landing-view.test.js` faalt op “Hourly detail”; `web/src/components/ReTerminalComparison.vue` noemt “6 month battery”; `docs/testing/e1003-touch-doze.md` vermeldt dat hosttests stroomverbruik en hardwaregebaren niet bewijzen.
- Waarom: verkoopbeloften moeten overeenkomen met het complete Windpeek-gebruik.
- Nadeel: fysieke metingen kosten doorlooptijd.
- Vertrouwen: 95%. Complexiteit: middel.

### 5. Homepage losmaken van de volledige configurator

- Voorstel: laat de landing met een lichte voorbeeldweergave werken; laad catalogus en configuratorgegevens wanneer die nodig zijn. Meet op een echte mobiele verbinding.
- As: onderhoud en laadsnelheid.
- Basis — direct: `LandingHero.vue` importeert de configuratorstore; die importeert `SPOTS`. De productiebuild bevat een configuratorchunk van circa 1,51 MB ongecomprimeerd, 366 kB gzip.
- Waarom: minder initiële download en werk voor een bezoeker die het product nog bekijkt. Dit bewijst op zichzelf geen trage ervaring; veldmetingen ontbreken.
- Externe basis: [web.dev over performance en conversiemeting](https://web.dev/articles/how-can-performance-improve-conversion).
- Nadeel: persoonlijke liveweergave op de homepage kan later verschijnen.
- Vertrouwen: 90% in technische kans. Complexiteit: middel.

### 6. Gericht versimpelen

- Voorstel: scheid spotconfiguraties, forecastladen en previewpublicatie waar dit verantwoordelijkheden duidelijker maakt. Centraliseer herhaalde apparaatkenmerken en voeg een lichte lintcontrole toe.
- As: onderhoud en laadsnelheid.
- Basis — direct: `web/src/stores/configurator.js` combineert die verantwoordelijkheden in ongeveer 591 regels; marketingmodellen staan in `ReTerminalComparison.vue`, configuratie kent eigen boarddefinities. `web/package.json` heeft geen lintscript.
- Waarom: wijzigingen aan tien spots en nieuwe apparaten worden beter te overzien.
- Nadeel: opsplitsen kan ook extra indirection toevoegen; alleen uitvoeren met een concreet voordeel. Behoud rendererpariteit, afzonderlijke foutafhandeling en installerherstel.
- Vertrouwen: 75%. Complexiteit: middel.

### 7. Eén feature rond de volgende goede sessie onderzoeken

- Voorstel: eenvoudige sportpresets en daarna een indicatie van kansrijke uren op basis van door de gebruiker gekozen wind- en golfvoorkeuren. Test dit eerst met vijf surfers/kiters. Gebruik hun eigen gedeelde spotconfiguraties voor een kleine communitypilot.
- As: nieuwe gebruikerswaarde.
- Basis — reasoned: het bestaande verhaal belooft veranderende omstandigheden op tijd zien; modellen en schermmodules bestaan al. Een begrijpelijke sessie-indicatie verbindt die gegevens aan die belofte.
- Waarom: helpt een beslissing nemen, bovenop meer weersinformatie tonen.
- Nadeel: voorkeuren verschillen sterk; presenteer het als indicatie, nooit als veiligheidsadvies. Geen bewijs van vraag zonder gebruikerstest.
- Vertrouwen: 60%. Complexiteit: middel–hoog.

## Rejection Summary

| Idee | Reden om nu te laten liggen |
|---|---|
| Volledige rewrite of TypeScriptmigratie | Geen aangetoond probleem dat deze omvang rechtvaardigt. |
| Alle forecastclients samenvoegen | Verschillende beschikbaarheid en fouten zijn bewust apart gehouden. |
| Legacy-firmware blind verwijderen | Nog gekoppeld aan ondersteunde buildkeuzes. |
| Accounts en cloudsync | Deelbare configuraties bestaan al; introduceert extra drempels en beheer. |
| Pushalerts en companion-app | Meer infrastructuur; behoefte eerst toetsen aan het rustige, zichtbare scherm. |
| Meer modellen en instellingen | Reeds breed aanbod; eerst begrijpelijkheid en activatie verbeteren. |
| Grote advertentiecampagne | Eerst funnel meten en installatie-uitval begrijpen. |
| Breed designherontwerp | De concrete kansen liggen in route, vertrouwen en voortgang; geen bewijs voor totale restyling. |

Voor groei via affiliate-inkomsten verdient ook de API-afspraak verificatie: de [Open-Meteo Free API-voorwaarden](https://open-meteo.com/en/terms) gelden voor niet-commercieel gebruik. Laat de aanbieder bevestigen welke afspraak bij dit gebruik past; dit document stelt geen overtreding vast.
