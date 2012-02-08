GTTHREAD_DIR=gtthreads
MATRIX_DIR=gtmatrix

all: release

release:
	$(MAKE) -C $(GTTHREAD_DIR)/Release/
	$(MAKE) -C $(MATRIX_DIR)/Release/
	cp $(GTTHREAD_DIR)/Release/libgtthreads.a .
	cp $(MATRIX_DIR)/Release/matrix .

debug:
	$(MAKE) -C $(GTTHREAD_DIR)/Debug/
	$(MAKE) -C $(MATRIX_DIR)/Debug/
	cp $(GTTHREAD_DIR)/Debug/libgtthreads.a .
	cp $(MATRIX_DIR)/Debug/matrix .

clean:
	$(MAKE) clean -C $(GTTHREAD_DIR)/Debug/
	$(MAKE) clean -C $(GTTHREAD_DIR)/Release/
	$(MAKE) clean -C $(MATRIX_DIR)/Debug/
	$(MAKE) clean -C $(MATRIX_DIR)/Release/
	rm -f matrix libgtthreads.a
