#define HERMES_REPORT_WARN
#define HERMES_REPORT_INFO
#define HERMES_REPORT_VERBOSE
#define HERMES_REPORT_FILE "application.log"
#include "hermes2d.h"

using namespace RefinementSelectors;

// This test makes sure that example 36-markers-nontypical works correctly.

int P_INIT = 2;                                   // Initial polynomial degree of all mesh elements.
const double THRESHOLD = 0.3;                     // This is a quantitative parameter of the adapt(...) function and
                                                  // it has different meanings for various adaptive strategies (see below).
const int STRATEGY = 0;                           // Adaptive strategy:
                                                  // STRATEGY = 0 ... refine elements until sqrt(THRESHOLD) times total
                                                  //   error is processed. If more elements have similar errors, refine
                                                  //   all to keep the mesh symmetric.
                                                  // STRATEGY = 1 ... refine all elements whose error is larger
                                                  //   than THRESHOLD times maximum element error.
                                                  // STRATEGY = 2 ... refine all elements whose error is larger
                                                  //   than THRESHOLD.
                                                  // More adaptive strategies can be created in adapt_ortho_h1.cpp.
const CandList CAND_LIST = H2D_HP_ANISO;          // Predefined list of element refinement candidates. Possible values are
                                                  // H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO,
                                                  // H2D_HP_ANISO_H, H2D_HP_ANISO_P, H2D_HP_ANISO.
                                                  // See User Documentation.
const int MESH_REGULARITY = -1;                   // Maximum allowed level of hanging nodes:
                                                  // MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
                                                  // MESH_REGULARITY = 1 ... at most one-level hanging nodes,
                                                  // MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
                                                  // Note that regular meshes are not supported, this is due to
                                                  // their notoriously bad performance.
const double CONV_EXP = 1.0;                      // Default value is 1.0. This parameter influences the selection of
                                                  // cancidates in hp-adaptivity. See get_optimal_refinement() for details.
const double ERR_STOP = 1.0;                      // Stopping criterion for adaptivity (rel. error tolerance between the
                                                  // reference mesh and coarse mesh solution in percent).
const int NDOF_STOP = 60000;                      // Adaptivity process stops when the number of degrees of freedom grows
                                                  // over this limit. This is to prevent h-adaptivity to go on forever.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  // Possibilities: SOLVER_AMESOS, SOLVER_AZTECOO, SOLVER_MUMPS,
                                                  // SOLVER_PARDISO, SOLVER_PETSC, SOLVER_SUPERLU, SOLVER_UMFPACK.

// Right-hand side.
const double RHS = 1.0;

// Material markers.
const int SOUTH_EAST = 10;
const int NORTH_EAST = 20;
const int NORTH_WEST = 30;
const int SOUTH_WEST = 40;

// Corresponding material constants.
const double A_SE = 1.0;
const double A_NE = 1.5;
const double A_NW = 0.5;
const double A_SW = 2.0;

// Boundary markers.
const int BDY_BOTTOM = 1;
const int BDY_VERTICAL = 2;
const int BDY_TOP = 3;

// Weak forms.
#include "forms.cpp"

int main(int argc, char* argv[])
{
  // Load the mesh.
  Mesh mesh;
  H2DReader mloader;
  mloader.load("domain.mesh", &mesh);

  // Enter boundary markers.
  BCTypes bc_types;
  bc_types.add_bc_dirichlet(BDY_BOTTOM);
  bc_types.add_bc_neumann(Hermes::vector<int>(BDY_VERTICAL, BDY_TOP));

  // Enter Dirichlet boundary values.
  BCValues bc_values;
  bc_values.add_zero(BDY_BOTTOM);

  // Create an H1 space with default shapeset.
  H1Space space(&mesh, &bc_types, &bc_values, P_INIT);

  // Initialize the weak formulation.
  WeakForm wf;
  wf.add_matrix_form(callback(bilinear_form_vol));
  wf.add_vector_form(callback(linear_form_vol));
  wf.add_vector_form_surf(callback(linear_form_surf));

  // Initialize refinement selector.
  H1ProjBasedSelector selector(CAND_LIST, CONV_EXP, H2DRS_DEFAULT_ORDER);

  // DOF and CPU convergence graphs.
  SimpleGraph graph_dof, graph_cpu;

  // Time measurement.
  TimePeriod cpu_time;
  cpu_time.tick();

  // Adaptivity loop:
  int as = 1;
  bool done = false;
  do
  {
    info("---- Adaptivity step %d:", as);

    // Construct globally refined reference mesh and setup reference space.
    Space* ref_space = construct_refined_space(&space);

    // Assemble the reference problem.
    info("Solving on reference mesh.");
    bool is_linear = true;
    DiscreteProblem* dp = new DiscreteProblem(&wf, ref_space, is_linear);
    SparseMatrix* matrix = create_matrix(matrix_solver);
    Vector* rhs = create_vector(matrix_solver);
    Solver* solver = create_linear_solver(matrix_solver, matrix, rhs);
    dp->assemble(matrix, rhs);

    // Time measurement.
    cpu_time.tick();

    // Solve the linear system of the reference problem. If successful, obtain the solution.
    Solution ref_sln;
    if(solver->solve()) Solution::vector_to_solution(solver->get_solution(), ref_space, &ref_sln);
    else error ("Matrix solver failed.\n");

    // Time measurement.
    cpu_time.tick();

    // Project the fine mesh solution onto the coarse mesh.
    Solution sln;
    info("Projecting reference solution on the coarse mesh.");
    OGProjection::project_global(&space, &ref_sln, &sln, matrix_solver);

    // Calculate element errors and total error estimate.
    info("Calculating error estimate.");
    Adapt* adaptivity = new Adapt(&space);
    double err_est_rel = adaptivity->calc_err_est(&sln, &ref_sln) * 100;

    // Report results.
    info("ndof_coarse: %d, ndof_fine: %d, err_est_rel: %g%%", 
         Space::get_num_dofs(&space), Space::get_num_dofs(ref_space), err_est_rel);

    // Time measurement.
    cpu_time.tick();

    // Add entry to DOF and CPU convergence graphs.
    graph_dof.add_values(Space::get_num_dofs(&space), err_est_rel);
    graph_dof.save("conv_dof_est.dat");
    graph_cpu.add_values(cpu_time.accumulated(), err_est_rel);
    graph_cpu.save("conv_cpu_est.dat");

    // If err_est too large, adapt the mesh.
    if (err_est_rel < ERR_STOP) done = true;
    else
    {
      info("Adapting coarse mesh.");
      done = adaptivity->adapt(&selector, THRESHOLD, STRATEGY, MESH_REGULARITY);

      // Increase the counter of performed adaptivity steps.
      if (done == false)  as++;
    }
    if (Space::get_num_dofs(&space) >= NDOF_STOP) done = true;

    // Clean up.
    delete solver;
    delete matrix;
    delete rhs;
    delete adaptivity;
    if(done == false) delete ref_space->get_mesh();
    delete ref_space;
    delete dp;

  }
  while (done == false);

  verbose("Total running time: %g s", cpu_time.accumulated());
  int ndof = Space::get_num_dofs(&space);

  printf("ndof allowed = %d\n", 210);
  printf("ndof actual = %d\n", ndof);
  if (ndof < 210) {      // ndofs was 206 at the time this test was created
    printf("Success!\n");
    return ERR_SUCCESS;
  }
  else {
    printf("Failure!\n");
    return ERR_FAILURE;
  }
}
