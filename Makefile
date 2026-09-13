PYTHON ?= python
DIST_DIR ?= dist
WHEEL_DIR ?= $(DIST_DIR)/wheels

.PHONY: native install test selfhost selfhost-test wheel ci-wheels sdist dist clean

native:
	$(PYTHON) setup.py build_ext --inplace

install:
	$(PYTHON) -m pip install -e .

test: native
	$(PYTHON) -m unittest discover -s tests -v
	$(PYTHON) -m unittest discover -s selfhost/tests -v

selfhost: native
	$(MAKE) -C selfhost stages

selfhost-test:
	$(MAKE) -C selfhost test

wheel:
	mkdir -p $(WHEEL_DIR)
	$(PYTHON) -m pip wheel --no-deps --no-build-isolation --wheel-dir $(WHEEL_DIR) .

ci-wheels:
	mkdir -p $(WHEEL_DIR)
	$(PYTHON) -m cibuildwheel --output-dir $(WHEEL_DIR)

sdist:
	$(PYTHON) setup.py sdist --dist-dir $(DIST_DIR)

dist: wheel sdist

clean:
	rm -rf build $(DIST_DIR)
	find dynphony -name '_native*.so' -delete
	find dynphony -name '_native*.pyd' -delete
