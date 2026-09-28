#!/usr/bin/env python3
"""Regenerates the built-in word prediction data.

  en-words.tsv    word<TAB>score   (score = Zipf frequency x 100, from wordfreq)
  en-bigrams.tsv  first<TAB>next<TAB>count   (from corpus.txt; "<s>" = sentence start)
  en-trigrams.tsv first second<TAB>next<TAB>count

Usage: pip install wordfreq && python3 generate.py
Words are lower case except proper nouns that are always written with a capital
(days, months, countries...). Names, abbreviations and slurs/explicit words are
left out; the predictor still learns any word the user actually says.
"""
import collections
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
WORD_COUNT = 6000
BIGRAM_COUNT = 3000
TRIGRAM_COUNT = 4000

SHORT_OK = set("a i am an as at be by do go he hi if in is it me my no of oh ok on or so to up us we "
               "ah uh um aw ha yo ya ex tv pc".split())

CAPITALISED = {w.lower(): w for w in """
Monday Tuesday Wednesday Thursday Friday Saturday Sunday
January February April June July August September October November December
Christmas Halloween Easter God Jesus Christ Christian Christians Christianity Bible
Muslim Muslims Islam Islamic Jewish Jews Catholic
America American Americans Britain British England English Scotland Scottish Ireland Irish
France French Germany German Germans Spain Spanish Italy Italian China Chinese Japan Japanese
Russia Russian Russians Europe European Africa African Asia Asian Australia Australian
Canada Canadian India Indian Mexico Mexican London Paris York California Texas Florida
TV PC Facebook YouTube Google Twitter Instagram Netflix Xbox Nintendo Pokemon Android
Microsoft Amazon Disney iPhone iPad Samsung Sony
""".split()}

EXCLUDED = set("""
mr mrs ms dr st co al etc ii iii iv vi xi http https com www inc ltd vs pre non anti multi
de la le les el en et du di da des der von san los las id im ll ve re pm uk usa eu un
bbc cnn cbs abc nfl nba fifa nasa fbi cia gop dna hiv ceo phd ios dc ny nyc ca va md mt nc ne
ga fl fr ss os rs sc ct lt cc gm hp hd dvd cd ft km kg cm mm mg lbs pp mp mps op ap ep ac fc
ab ar bc jr sr jan feb aug sept oct nov dec mar vol th rd nd min em ed q x y z
dont doesnt didnt cant thats wont isnt wasnt youre theyre ive
shit fuck fucking fucked fuckin bullshit asshole bitch ass dick cock pussy penis porn rape raped
nazi hitler wtf piss don del sri rio gen rep est
john david james michael george paul william robert thomas louis chris richard charles scott
steve johnson jones harry henry peter mike tom joe bob jim sam ben ray lee kim nick ryan jackson
adam andrew kevin matt stephen simon brian elizabeth donald jason dave harris andy charlie
anthony arthur alexander francis gary allen gordon matthew patrick phil wayne anderson austin
diego johnny bruce kate russell anne ian justin hamilton jon jonathan lawrence lincoln oscar sean
lisa moore philip thompson murray alice cameron danny roy stewart albert marshall mitchell susan
aaron greg jacob leo margaret mario christopher oliver ron douglas fred jose barbara helen
jennifer marie samuel stanley craig antonio jamie jessica keith tyler michelle neil harvey jeremy
rachel eddie emily nathan stuart juan julia catherine dennis carlos julie karen todd hughes linda
stan mary tony jimmy kelly morgan jane anna roger ross clark jay walter jean carter billy rick
ali bobby evans perry ken amy wright chuck jake kent benjamin blake tommy collins morris kyle
nancy norman vincent pete brad ralph colin marcus lucas rogers ashley emma laura parker nelson
terry steven carl franklin cole earl owen dylan ellen hannah ruth larry graham barry khan warren
tim dan ted ann max davis wilson williams edward joseph howard kennedy watson robinson campbell
harrison holmes spencer bailey miller smith trump obama clinton hillary putin martin jordan
lewis taylor eric jeff luke jerry sarah daniel alex cooper victoria dean
washington chicago boston angeles francisco jersey carolina virginia ohio georgia illinois
michigan pennsylvania columbia arizona atlanta kansas iowa indiana massachusetts minnesota
kentucky oregon oklahoma alabama missouri maryland utah baltimore charlotte denver houston dallas
detroit seattle philadelphia vegas miami toronto sydney melbourne manchester liverpool glasgow
edinburgh birmingham cambridge oxford harvard rome moscow madrid dublin delhi tokyo beijing
jerusalem berlin barcelona milan montreal vancouver ontario queensland newcastle pittsburgh
cleveland portland richmond brooklyn manhattan hollywood hawaii alaska louisiana mississippi
maine tennessee wisconsin connecticut dakota arkansas montana sierra orleans singapore nigeria
nigerian pakistan iran iranian iraq israel israeli egypt egyptian syria syrian saudi arabia arab
afghanistan korea korean vietnam thailand thai malaysia philippines indonesia kenya cuba brazil
brazilian argentina chile poland portugal sweden swedish norway belgium holland netherlands
austria switzerland swiss greece greek turkish ukraine ukrainian dutch welsh wales zealand hong
kong caribbean atlantic victorian chelsea arsenal isis
""".split())


def words():
    from wordfreq import top_n_list, zipf_frequency
    out = []
    for w in top_n_list('en', 20000):
        if not re.fullmatch(r"[a-z]+(?:'[a-z]+)?", w):
            continue
        if len(w) <= 2 and w not in SHORT_OK:
            continue
        if w in EXCLUDED:
            continue
        # Possessives ("people's") are noise; keep real contractions.
        if w.endswith("'s") and w[:-2] not in {"it", "he", "she", "that", "there", "here", "what",
                                               "who", "where", "let", "how", "when", "why"}:
            continue
        out.append((CAPITALISED.get(w, w), int(round(zipf_frequency(w, 'en') * 100))))
        if len(out) >= WORD_COUNT:
            break
    return out


def ngrams():
    bi = collections.Counter()
    tri = collections.Counter()
    with open(os.path.join(HERE, 'corpus.txt'), encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            for sentence in re.split(r"[.!?]+", line):
                tokens = [t.lower() for t in re.findall(r"[A-Za-z]+(?:'[A-Za-z]+)?", sentence)]
                if not tokens:
                    continue
                seq = ['<s>'] + tokens
                for a, b in zip(seq, seq[1:]):
                    bi[(a, b)] += 1
                for a, b, c in zip(seq, seq[1:], seq[2:]):
                    tri[(a + ' ' + b, c)] += 1

    def ranked(counter, n):
        return sorted(counter.items(), key=lambda kv: (-kv[1], kv[0]))[:n]
    return ranked(bi, BIGRAM_COUNT), ranked(tri, TRIGRAM_COUNT)


def main():
    with open(os.path.join(HERE, 'en-words.tsv'), 'w', encoding='utf-8', newline='\n') as f:
        f.write("# English word frequencies (Zipf x 100) from wordfreq by Robyn Speer,\n"
                "# https://github.com/rspeer/wordfreq - data licensed CC BY-SA 4.0. Generated by generate.py.\n")
        for w, score in words():
            f.write(f"{w}\t{score}\n")
    bi, tri = ngrams()
    for name, rows in (('en-bigrams.tsv', bi), ('en-trigrams.tsv', tri)):
        with open(os.path.join(HERE, name), 'w', encoding='utf-8', newline='\n') as f:
            f.write("# Next-word counts from corpus.txt (public domain). Generated by generate.py.\n")
            for (a, b), n in rows:
                f.write(f"{a}\t{b}\t{n}\n")


if __name__ == '__main__':
    main()
