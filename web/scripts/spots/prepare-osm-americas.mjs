#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { importOsmElements } from './lib/osm-source.mjs';
const base=path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../data/spots')+'/';
if (!process.argv[2] || !process.argv[3]) throw new Error('Usage: node scripts/spots/prepare-osm-americas.mjs <overpass.json> <snapshot.json>');
const raw=JSON.parse(fs.readFileSync(process.argv[2]));
const previous=JSON.parse(fs.readFileSync(base+'sources/osm-wind-watersports-2026-08-27.json'));
const known=new Set(importOsmElements(previous.elements).candidates.map(e=>e.sourceId));
const exclusions=[],elements=[];
const manual=new Map([
 ['way/1033350924','kite-school'],['way/1215434113','nautical-club'],['way/642325763','generic-name'],
 ['node/12613258243','surf-school'],['node/5013108125','artificial-wave'],['way/1337308826','artificial-wave'],['way/708131365','artificial-wave'],['way/832627843','training-centre'],['way/1005960945','surf-school'],
 ['node/11292465329','river-wave'],['node/13233237042','river-wave'],['node/665893503','river-wave'],['node/7576438148','river-wave'],['node/8019484088','river-wave'],['node/897913777','river-wave'],
 ['node/2559834996','generic-name'],['node/4371550071','surf-school-and-rental'],['node/4611994456','business'],['node/5183023518','unclear-spot'],['node/4549692986','dive-centre'],['node/4574878963','surf-camp'],['node/3659150483','kapoho-2018-lava-coast-change'],['node/3659150484','kapoho-2018-lava-coast-change'],['node/3659150485','kapoho-2018-lava-coast-change'],
]);
for(const rawElement of raw.elements){
 const e=structuredClone(rawElement),id=`${e.type}/${e.id}`,t=e.tags??{};
 if(!e.center&&e.bounds) e.center={lat:(e.bounds.minlat+e.bounds.maxlat)/2,lon:(e.bounds.minlon+e.bounds.maxlon)/2};
 const lat=e.lat??e.center?.lat,lon=e.lon??e.center?.lon;
 let reason= known.has(id)?'already-in-pinned-osm-source': !t.name?'unnamed':lat<0&&lon<-100?'outside-americas':manual.get(id);
 if(!reason && (['shop','office','school','club','building'].some(k=>t[k] && !['no','false','0'].includes(t[k])) || ['hotel','camp_site','hostel','guest_house'].includes(t.tourism) || (t.amenity&&!['no','false','0'].includes(t.amenity)&&!['slipway'].includes(t.amenity)) || ['water_park','swimming_pool','whitewater_course','fitness_centre'].includes(t.leisure)))reason='business-facility-or-artificial-wave';
 if(!reason && /\b(school|escola|escuela|ecole|shop|club|lodge|lodging|hotel|dojo|fitness|whitewater|center|centre)\b/i.test(t.name.normalize('NFKD').replace(/[\u0300-\u036f]/g,'')))reason='business-facility-or-artificial-wave';
 if(!reason){ const r=importOsmElements([e]); if(!r.candidates.length)reason=r.exclusions[0]?.reason??r.failures[0]?.reason; else if(r.candidates[0].flags.length)reason=r.candidates[0].flags.join(','); }
 if(reason){exclusions.push({sourceId:id,name:t.name??'',reason});continue;}
 const keep=['name','sport','activity','sport:secondary','natural','leisure','waterway','man_made','amenity','addr:country','is_in:country_code','access'];
 e.tags=Object.fromEntries(Object.entries(t).filter(([k])=>keep.includes(k)));
 elements.push(e);
}
const out={version:1,osm3s:raw.osm3s,query:'nwr["sport"~"(^|;)(surfing|kitesurfing|kiteboarding|kite_surfing|windsurfing|wingfoil|wingfoiling|wing_foil|wing_foiling)(;|$)"](-56,-170,75,-30); out tags bb;',selection:'Named physical spots in the Americas; excludes existing source IDs, businesses, artificial/river waves, large geometries and explicit review exclusions. Way/relation centres derived from bounding boxes.',rawCount:raw.elements.length,elements,exclusions};
fs.writeFileSync(process.argv[3],JSON.stringify(out,null,2)+'\n');
console.log('selected',elements.length);console.log(exclusions.reduce((a,e)=>(a[e.reason]=(a[e.reason]??0)+1,a),{}));
