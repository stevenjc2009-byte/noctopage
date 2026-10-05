# opds work package (src/net: xml*.c, url.c, opds*.c). The test's own module is linked
# automatically; everything else is listed here. cJSON is the vendored parser (opds_json.c needs
# its header). OPDS_SRCS is every opds/xml/url source: any test of the opds API links all of them.
DEPS_xml    = ../src/net/xml_util.c
OPDS_SRCS   = ../src/net/opds_build.c ../src/net/opds_atom.c ../src/net/opds_json.c ../src/net/opds_text.c \
              ../src/net/xml.c ../src/net/xml_util.c ../src/net/url.c $(BUILD)/cJSON.o
DEPS_opds   = $(OPDS_SRCS)
CFLAGS_opds = -I../third_party/cjson
DEPS_opds_edge   = ../src/net/opds.c $(OPDS_SRCS)
CFLAGS_opds_edge = -I../third_party/cjson
