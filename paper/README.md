# Paper draft

`main.tex` is the skeleton (ACM `acmart`, `sigconf` two-column style, `nonacm` so no copyright block). `refs.bib` holds starter references,
every one flagged VERIFY: they were written from memory and must be checked against the sources before use.
`template/` holds the ACM class sources, downloaded from CTAN (`acmart.zip`, LaTeX Project Public License): `acmart.ins`, `acmart.dtx`,
`ACM-Reference-Format.bst`, `README`.

## Build

`acmart.cls` is not stored: it is generated from the sources. No TeX installation was available on this machine when this was written, so
the skeleton has **not been compiled**. With TeX Live (`texlive-publishers` has `acmart` already; otherwise generate it):

    cd paper/template && latex acmart.ins && cp acmart.cls ..       # only if acmart.cls is not already installed
    cd paper && pdflatex main && bibtex main && pdflatex main && pdflatex main

Alternatively paste `main.tex` and `refs.bib` into Overleaf (it provides `acmart`).

## Why acmart

PSB1 and PSB2 (the benchmark this work reports on) and PushGP were published at GECCO, which uses `acmart` `sigconf`. The two-column
layout is the same family as the arXiv paper that was given as the model. If a different venue is chosen (IEEE `IEEEtran`, NeurIPS,
ICML), only the first lines of `main.tex` change.

## Source of the numbers

All numbers come from `docs/claims.md` and `docs/findings/` (section numbers are in the comments of `main.tex`). Nothing should enter the
paper that is not in `docs/claims.md` with its caveat.
