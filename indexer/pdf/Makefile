%.pdf: ../$* 
	vim -R -c "hardcopy > $*.ps" -c "quit" ../$*
	ps2pdf $*.ps $*.pdf
	rm $*.ps

clean:
	rm -f *.pdf
	rm -f *.ps
