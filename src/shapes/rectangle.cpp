#include <lightwave.hpp>
#include "rectangle.hpp"

// this informs lightwave to use our class Rectangle whenever a <shape
// type="rectangle" /> is found in a scene file
REGISTER_SHAPE(Rectangle, "rectangle")
