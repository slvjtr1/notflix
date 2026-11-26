"""Convert upstream Android string resources used by the native TV interface."""
from pathlib import Path
import sys
import json
import re
import xml.etree.ElementTree as ET

root=Path(__file__).resolve().parents[1]
resources=Path(sys.argv[1])/"app/src/main/res"
out=root/"package/assets/i18n"
out.mkdir(exist_ok=True,parents=True)
def dictionary(path):
    return {s.get("name"): "".join(s.itertext()).replace("\\'","'").replace('\\"','"')
            for s in ET.parse(path).getroot().findall("string")}
base=dictionary(resources/"values/strings.xml")
source="\n".join(p.read_text(encoding="utf8") for p in (root/"src").glob("*.c"))
used=set(re.findall(r'"([^"\n]{1,90})"',source))
names={s.casefold():s for s in used}
languages={"en":"English","ar":"العربية","es-419":"Español (Latinoamérica)","sr-Latn":"Srpski",
"bg":"Български","bn":"বাংলা","bs":"Bosanski","cs":"Čeština","da":"Dansk","de":"Deutsch",
"el":"Ελληνικά","es":"Español","fr":"Français","hi":"हिन्दी","hr":"Hrvatski","hu":"Magyar",
"id":"Indonesia","it":"Italiano","he":"עברית","ja":"日本語","lt":"Lietuvių","nl":"Nederlands",
"no":"Norsk","pl":"Polski","pt-BR":"Português (Brasil)","pt-PT":"Português (Portugal)",
"ro":"Română","ru":"Русский","sk":"Slovenčina","sl":"Slovenščina","sq":"Shqip","sv":"Svenska",
"ta":"தமிழ்","tr":"Türkçe","uk":"Українська","vi":"Tiếng Việt","zh-CN":"简体中文","zh-TW":"繁體中文"}
def dump(path,obj):
    path.write_text(json.dumps(obj,ensure_ascii=False,indent=2,sort_keys=True)+"\n",encoding="utf8")
present={"en":languages["en"]}
dump(out/"en.json",{})
for f in sorted(resources.glob("values-*/strings.xml")):
    lang=f.parent.name[7:].replace("b+","").replace("+","-").replace("-r","-")
    lang={"iw":"he","in":"id"}.get(lang,lang)
    if lang not in languages:continue
    values=dictionary(f)
    translations={names[en.casefold()]:values[k] for k,en in base.items()
                  if k in values and en.casefold() in names and "%" not in en and values[k].strip()}
    dump(out/(lang+".json"),translations)
    present[lang]=languages[lang]
dump(out/"index.json",present)
print(f"Converted {len(present)} interface languages.")

