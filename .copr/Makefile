srpm:
	dnf -y install git rpm-build rpmlint
	git config --global --add safe.directory '*'
	./packaging/build-srpm.sh --head
	cp $$HOME/rpmbuild/SRPMS/*.src.rpm "$(outdir)/"
