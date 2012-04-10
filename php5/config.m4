PHP_ARG_ENABLE(bluecherry, Bluecherry extension,
[ --enable-bluecherry   Enable bluecherry extension])

if test "$PHP_BLUECHERRY" = "yes"; then
	PHP_REQUIRE_CXX()
	AC_DEFINE(HAVE_BLUECHERRY, 1, [Whether you have bluecherry])
	PHP_ADD_LIBRARY(stdc++, 1, BLUECHERRY_SHARED_LIBADD)
	PHP_NEW_EXTENSION(bluecherry, bluecherry.cpp, $ext_shared)
	PHP_ADD_LIBPATH([../lib], )
	PHP_ADD_LIBRARY([bluecherry],, BLUECHERRY_SHARED_LIBADD)
	PHP_ADD_INCLUDE([../lib])
	PHP_SUBST(BLUECHERRY_SHARED_LIBADD)
fi
