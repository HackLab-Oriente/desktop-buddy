"""Scale test: Markov over CESS-ESP, a real Spanish corpus ~100x our demo.

    pip install nltk && python3 cess_scale_test.py

The point is not CESS itself — it is news prose, nothing like buddy quips.
The point is the CONTROL: measuring full sentences (30 words average) against
short ones (<=12 words) at the same corpus sizes separates two effects that
are easy to confuse — how much a corpus grows novelty, and how much sentence
LENGTH does. Reading only the full-corpus numbers would suggest Markov is far
more generative than it is at our sentence length.

Needs nltk; markov.py stays stdlib-only on purpose.
"""
import collections, random, re, statistics
from nltk.corpus import cess_esp

BEGIN, END = "\x02", "\x03"
TAG = re.compile(r"^-[A-Za-z]+-$")          # -Fpa-, -Fpt-: marcas del treebank

def clean(sent):
    out = []
    for w in sent:
        if TAG.match(w) or w == "*0*":
            continue
        out.append(w.replace("_", " "))
    s = " ".join(out)
    s = re.sub(r"\s+([,.;:!?])", r"\1", s).strip()
    return s

def build(lines, order):
    t = collections.defaultdict(list)
    for s in lines:
        toks = [BEGIN]*order + s.split() + [END]
        for i in range(len(toks)-order):
            t[tuple(toks[i:i+order])].append(toks[i+order])
    return t

def gen(t, order, limit=60):
    st, out = (BEGIN,)*order, []
    for _ in range(limit):
        nx = t.get(st)
        if not nx: break
        w = random.choice(nx)
        if w == END: break
        out.append(w); st = (st+(w,))[1:]
    return " ".join(out)

def measure(lines, order, tries=600):
    t = build(lines, order)
    known = set(lines)
    outs = [g for g in (gen(t, order) for _ in range(tries)) if g]
    novel = [o for o in outs if o not in known]
    return {"novelty": len(novel)/len(outs)*100,
            "distinct": len(set(novel)),
            "avglen": statistics.mean(len(o.split()) for o in outs),
            "states": len(t)}

raw = [clean(s) for s in cess_esp.sents()]
raw = [s for s in raw if len(s.split()) >= 3]
short = [s for s in raw if len(s.split()) <= 12]
print(f"CESS-ESP completo : {len(raw)} frases, media {statistics.mean(len(s.split()) for s in raw):.1f} palabras")
print(f"Solo frases cortas: {len(short)} frases, media {statistics.mean(len(s.split()) for s in short):.1f} palabras\n")

random.seed(5)
for name, corpus in (("COMPLETO (prosa de prensa)", raw), ("CORTAS <=12 palabras", short)):
    print(f"--- {name} " + "-"*(46-len(name)))
    print(f"{'frases':>8} {'orden':>6} {'novedad':>9} {'distintas':>10} {'estados':>9} {'long.media':>11}")
    for n in (60, 200, 1000, 3000, len(corpus)):
        if n > len(corpus): continue
        sub = random.sample(corpus, n)
        for order in (1, 2, 3):
            m = measure(sub, order)
            print(f"{n:>8} {order:>6} {m['novelty']:>8.1f}% {m['distinct']:>10} {m['states']:>9} {m['avglen']:>10.1f}")
        print()
