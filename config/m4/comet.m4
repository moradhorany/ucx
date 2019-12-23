AC_ARG_WITH([comet-hw],
            [AC_HELP_STRING([--with-comet-hw(=DIR)],
                            [Where to find the COMET libraries and header files])],
            [],
            [with_comet_hw=no])

AS_IF([test "x$with_comet_hw" != xno],
      [
      AC_CHECK_HEADERS([$with_comet_hw/src/libcomet.h], [comet_happy="yes"], [comet_happy="no"])
      AS_IF([test "x$comet_happy" = xyes],
            [
            AC_SUBST(COMET_CPPFLAGS,  "-I$with_comet_hw/src -I$with_rte/driver -I$with_rte/inc")
            #AC_SUBST(COMET_LDFLAGS,   "-L$with_comet_hw/lib -lcomet")
            AC_DEFINE([HAVE_COMET_HW], [1], [COMET support (experimental)])
            ], [])],
      [])
