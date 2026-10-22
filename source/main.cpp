#include "shared.h"
#include "tdu_instance.h"

int main( int argc, char **argv ) {
    return TestDrive::InitAndRun( const_cast<const char **>( argv ), argc );
}
