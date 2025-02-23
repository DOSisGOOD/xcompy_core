CORE_NAME = xcompy_core
CFLAGS = -Wall -fPIC -I.
LDFLAGS = -shared -lX11 -lXcomposite -lXfixes

$(CORE_NAME).so: $(CORE_NAME).c
	gcc -ggdb3 $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(CORE_NAME).so
