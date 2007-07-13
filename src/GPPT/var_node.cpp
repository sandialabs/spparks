
#include "math.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "var_node.h"
#include "spk.h"
#include "random_park.h"
#include "error.h"
#include <iostream>

using namespace std;
using namespace SPPARKS;

/* ---------------------------------------------------------------------- */

VarNode::VarNode() : Node()
{
  type = VARIABLE;
}
/* ---------------------------------------------------------------------- */
VarNode::~VarNode()
{
}
/* ---------------------------------------------------------------------- */
void VarNode::write(FILE * dest)
{
  fprintf(dest,"var[%d]",var_index);
  // fprintf(dest,"2.5");
}
/* ---------------------------------------------------------------------- */
void VarNode::write_tex(FILE * dest)
{
  fprintf(dest,"var[%d]",var_index);
}
/* ---------------------------------------------------------------------- */
void VarNode::write_stack(FILE * dest)
{
  fprintf(dest,"var[%d]",var_index);
}
/* ---------------------------------------------------------------------- */
double VarNode::go(double * var)
{
  return var[var_index];
}
/* ---------------------------------------------------------------------- */