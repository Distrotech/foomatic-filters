# src/Makefile

### Commented out is the production version and below the evaluation version.
SCRIPTS = cupsomatic directomatic lpdomatic ppromatic


scripts: $(SCRIPTS:%=../%)


clean:
	$(RM) $(SCRIPTS:%=../%)


### Pattern rules to build targets in the parent dir ###

## Perl scripts
../% : %.pl
ifeq ($(INPLACE),)
	sed -e '/<INPLACE>/,/<!INPLACE>/d;t; \
		/<PACKAGE>/,/<!PACKAGE>/{; \
		s/$$ETCDIR/$(subst /,\/,$(ETCDIR))/g; \
		s/$$EXECPATH/$(subst /,\/,$(EXEC_PATH))/g; \
		s/$$LOGPATH/$(subst /,\/,$(LOG_PATH))/g; \
		s/$$BASENAME/$(<:%.pl=%)/g;}' -e \
		'/<!\?PACKAGE>/d' $< > $@
else
	sed -e '/<PACKAGE>/,/<!PACKAGE>/d;t; \
		/<!\?INPLACE>/d' $< > $@
endif
	chmod a+x $@


## C progs
../% : %.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@