using(VRML)
using(boost)
using(exprtk)
covise_find_package(Boost COMPONENTS timer REQUIRED)

SET(HEADERS
  ColorMapShader.h
  CsvPointCloud.h
  DataTable.h  
  Interactor.h
  OctUtils.h
  RenderObject.h
  SurfacePrimitiveSet.h
)

SET(SOURCES
  ColorMapShader.cpp
  CsvPointCloud.cpp
  DataTable.cpp
  Interactor.cpp
  OctUtils.cpp
  RenderObject.cpp  
)

cover_add_plugin(CsvPointCloud)
target_link_libraries(CsvPointCloud Boost::timer exprtk)

