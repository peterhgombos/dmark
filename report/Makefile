all:
	pdflatex -jobname report main.tex
	bibtex report
	pdflatex -jobname report main.tex

clean:
	rm -rf *.aux report.pdf *.log *.out
