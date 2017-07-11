#include <array>
#include <vtkSmartPointer.h>
#include <vtkDoubleArray.h>
#include <vtkPolyData.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkSTLReader.h>
#include "vtkSmartPointer.h"
#include "vtkIdTypeArray.h"
#include "vtkPolyDataNormals.h"
#include "vtkCellData.h"
#include "vtkPointData.h"
#include "vtkDataArray.h"
#include <vtkXMLPolyDataWriter.h>
#include <vtkVector.h>
#include <vtkOBJReader.h>
#include <vtkXMLPolyDataReader.h>
#include <vtkUnstructuredGridReader.h>
#include <Eigen/Sparse>
#include <Eigen/Core>
#include <time.h>
#include <queue>
#include <set>

using namespace std;

const char* minor_name="Local Coord Minor Curvature Direction";
const char* euc_minor_name="Minor Curvature Direction";
const char* curvatures_name="Curvatures";
const double INV_ROOT_2 = 1/sqrt(2);


vtkVector3d operator+(vtkVector3d u, vtkVector3d v){
  vtkVector3d w;
  for(int foo=0;foo<3;foo++){
    w[foo]=u[foo]+v[foo];
  }
  return w;
};
vtkVector3d operator-(vtkVector3d u, vtkVector3d v){
  vtkVector3d w;
  for(int foo=0;foo<3;foo++){
    w[foo]=u[foo]-v[foo];
  }
  return w;
};
vtkVector3d operator*(double scalar, vtkVector3d v){
  vtkVector3d w;
  for(int foo=0;foo<3;foo++){
    w[foo]=scalar*v[foo];
  }
  return w;
};
template <typename T> int sgn(T val) {
    return (T(0) < val) - (val < T(0));
}




std::vector<int> ngb_triangles( int center_tri, vtkSmartPointer<vtkPolyData> surface){
  //vtk needs a good way to call the adjacent cells
  std::vector<int> ngbs;
  auto cell_pts = vtkSmartPointer<vtkIdList>::New();
  surface->GetCellPoints( center_tri , cell_pts);
  for(int bar=0; bar<3; bar ++){
    //Find neighboring cell across edge bar
    auto cell_ngb_wrap = vtkSmartPointer<vtkIdList>::New();
    surface->GetCellEdgeNeighbors( center_tri ,
      cell_pts->GetId( (bar+2)%3 ),
      cell_pts->GetId( (bar+1)%3 ),
      cell_ngb_wrap);
    if( cell_ngb_wrap->GetNumberOfIds()>0 ){
      int cell_ngb = cell_ngb_wrap->GetId(0);
      ngbs.push_back(cell_ngb);
    }
  }
  return ngbs;
}

vtkSmartPointer<vtkPolyData> build_small_simplicial_example()
{
  std::string example_name ("cylinder"); //"simple" "simple2" "cylinder" "grid"
  auto points = vtkSmartPointer<vtkPoints>::New();
  auto triangles = vtkSmartPointer<vtkCellArray>::New();

  if (example_name.compare("simple") ==0 ){
    //Choose some points in parametric space
    double parametric_pts[6][2] = {
      {0.0,0.0},
      {0.0,1.0},
      {0.0,2.0},
      {1.0,0.0},
      {1.0,1.0},
      {2.0,0.0}
    };
    //Apply a ellipsoid parametrization to obtain points in R3
    int num_pts = sizeof(parametric_pts)/sizeof(parametric_pts[0]);
    points->SetNumberOfPoints(num_pts);
    for (int foo = 0; foo<num_pts; foo++){
      double pt[3];
      double p1 = .1 + .1 * parametric_pts[foo][0];
      double p2 = .2 + .2 * parametric_pts[foo][1];
      pt[0] = 5*cos(p1)*cos(p2);
      pt[1] = 4*sin(p1)*cos(p2);
      pt[2] = 3*sin(p2);
      points->SetPoint(foo, pt[0], pt[1], pt[2]);
    }
    int cellList[4][3] = {
      {0,1,3},
      {1,3,4},
      {1,2,4},
      {3,4,5}
    };
    for(int foo = 0; foo<sizeof(cellList)/sizeof(cellList[0]); foo++){
      triangles->InsertNextCell(3);
      for(int bar = 0; bar<sizeof(cellList[0])/sizeof(cellList[0][0]); bar++){
        triangles->InsertCellPoint(cellList[foo][bar]);
      }
    }
  }

  if (example_name.compare("simple2") == 0)
  {
    double angle0 = 0.0, angle1 = M_PI/6 , angle2 = 0.0;
    double pt_pos[6][3] = {
      {0,0,0},
      {1,0,0},
      {0,1,0},
      {0,-1,tan(angle0)},
      {-1,0,tan(angle1)},
      {1,1,tan(angle2)/sqrt(2)}
    };
    int num_pts = sizeof(pt_pos)/sizeof(pt_pos[0]);
    points->SetNumberOfPoints(num_pts);
    for (vtkIdType foo = 0; foo < num_pts; foo++) {
      double p0 = pt_pos[foo][0], p1 = pt_pos[foo][1], p2 = pt_pos[foo][2];
      points->SetPoint(foo, p0, p1, p2);
    }
    int cellList[4][3] = {
      {0,1,2},
      {0,3,1},
      {0,2,4},
      {1,5,2}
    };
    for(int foo = 0; foo<sizeof(cellList)/sizeof(cellList[0]); foo++){
      triangles->InsertNextCell(sizeof(cellList[0])/sizeof(cellList[0][0]));
      for(int bar = 0; bar<sizeof(cellList[0])/sizeof(cellList[0][0]); bar++){
        triangles->InsertCellPoint(cellList[foo][bar]);
      }
    }
  }
  if (example_name.compare("cylinder") == 0)
  {
    double cyl_wobble_amp = .3, cyl_wobble_freq=2 , cylrad=1;
    int xtris = 70;
    int ytris = 70;
    double param_pts[xtris*(ytris+1)][2];
    int const num_tris = 2 * xtris * ytris;
    int cell_list[num_tris][3];
    for(int foo = 0 ; foo < num_tris; foo++){
      int const sq = floor(foo/2);
      int const is_down = foo%2;
      int const pt_col = sq % xtris;
      int const pt_row = floor(sq / xtris);
      if(not is_down){
        cell_list[foo][0]= pt_col  + xtris * pt_row;
        cell_list[foo][1]=(1 + pt_col) % xtris  + xtris * (pt_row);
        cell_list[foo][2]=0 + pt_col + xtris * (pt_row+1);
      }
      else{
        cell_list[foo][0]=(1 + pt_col) % xtris + xtris * pt_row;
        cell_list[foo][1]=(1 + pt_col) % xtris + xtris * (pt_row+1);
        cell_list[foo][2]=0 + pt_col  + xtris * (pt_row+1);
      }
    }
    double const y_shift=sqrt(3)/2;
    double const x_shift=1;
    for(int foo =0; foo < (xtris)*(ytris+1); foo++){
      int pt_col = foo%xtris;
      int pt_row = foo/xtris;
      param_pts[foo][0]=pt_col + 0.5*pt_row;
      param_pts[foo][1]=y_shift*pt_row;
    }
    //Apply parametrization to obtain points in R3
    int num_pts = sizeof(param_pts)/sizeof(param_pts[0]);
    points->SetNumberOfPoints(num_pts);
    for (int foo = 0; foo<num_pts; foo++){
      double pt[3];
      double p1 = param_pts[foo][0];
      double p2 = param_pts[foo][1];
      double rad_now = cyl_wobble_amp * cos(2*M_PI*cyl_wobble_freq*p2/ytris ) + cylrad;
      pt[0] = rad_now * cos(2*M_PI*p1/xtris);
      pt[1] = rad_now * sin(2*M_PI*p1/xtris);
      pt[2] = 2*M_PI*p2/ytris;
      points->SetPoint(foo, pt[0], pt[1], pt[2]);
    }
    for(int foo = 0; foo<sizeof(cell_list)/sizeof(cell_list[0]); foo++){
      triangles->InsertNextCell(3);
      for(int bar = 0; bar<sizeof(cell_list[0])/sizeof(cell_list[0][0]); bar++){
        triangles->InsertCellPoint(cell_list[foo][bar]);
      }
    }
  }
  if (example_name.compare("grid") == 0)
  {
    int gridx=30;
    int gridy=10;
    int num_pts=(gridx+1)*(gridy+1);
    double pt_pos[num_pts][3];
    for(int foo=0; foo<num_pts; foo++){
      pt_pos[foo][0] = (foo % (gridx+1));
      pt_pos[foo][1] = floor(foo / (gridx+1) );
      pt_pos[foo][2] = 0;
    }
    points->SetNumberOfPoints(num_pts);
    for (vtkIdType foo = 0; foo < num_pts; foo++) {
      double p0 = pt_pos[foo][0], p1 = pt_pos[foo][1], p2 = pt_pos[foo][2];
      points->SetPoint(foo, p0, p1, p2);
    }
    int num_triang = 2*gridx*gridy;
    int cellList[num_triang][3];
    for(int foo=0; foo<num_triang; foo++){
      int is_up = (foo+1) % 2;
      int sqre = foo / 2;
      int xplace = ( foo / 2 ) % (gridx);
      int yplace = ( ( foo/ 2 ) / (gridx));
      if(is_up){
        cellList[foo][0] = xplace + (gridx+1) * yplace;
        cellList[foo][1] = 1+xplace + (gridx+1) * yplace;
        cellList[foo][2] = xplace + (gridx+1) * (1+yplace);
      }
      else{
        cellList[foo][1] = 1+xplace + (gridx+1) * (1+yplace);
        cellList[foo][0] = 1+xplace + (gridx+1) * yplace;
        cellList[foo][2] = xplace + (gridx+1) * (1+yplace);
      }
    }
    for(int foo = 0; foo<num_triang; foo++){
      triangles->InsertNextCell(3);
      for(int bar = 0; bar<3; bar++){
        triangles->InsertCellPoint(cellList[foo][bar]);
      }
    }
  }

  auto simplicial_complex = vtkSmartPointer<vtkPolyData>::New();
  simplicial_complex->SetPoints(points);
  simplicial_complex->SetPolys(triangles);
  simplicial_complex->BuildCells();
  simplicial_complex->BuildLinks();
  return simplicial_complex;
}


//Define a structure for priority queue
struct cellPrior{
  int id;
  double confidence;

  cellPrior(int const id_, double const confidence_) : id{id_}, confidence{confidence_} {};
};
bool operator<(const cellPrior& cell1, const cellPrior& cell2){
  return fabs(cell1.confidence) < fabs(cell2.confidence);
}
//Function to align the vector field consistently
vtkSmartPointer<vtkPolyData> allign_field(vtkSmartPointer<vtkPolyData> surface){

  int const num_surf_cells = surface->GetNumberOfCells();
  auto cell_order = vtkSmartPointer<vtkIntArray>::New();
  cell_order->SetNumberOfComponents(1);
  cell_order->SetNumberOfTuples( num_surf_cells );
  cell_order->SetName("Cell Order");

  auto alligned_patch = vtkSmartPointer<vtkIntArray>::New();
  alligned_patch->SetNumberOfComponents(1);
  alligned_patch->SetNumberOfTuples( num_surf_cells );
  alligned_patch->SetName("Concurrence Patch");

  vtkSmartPointer<vtkDataArray> euc_field = surface->GetCellData()->GetArray(euc_minor_name);
  vtkSmartPointer<vtkDataArray> minor_curv_field = surface->GetCellData()->GetArray(minor_name);
  vtkSmartPointer<vtkDataArray> curvatures = surface->GetCellData()->GetArray(curvatures_name);

  //Partition the surface into patches where the field aligns locally within 45 degrees
  int flip_scheme[num_surf_cells];
  int cell_counter = 0;
  int patch_count = -1;
  bool cell_decided[num_surf_cells];
  std::priority_queue< cellPrior > cell_que;
  for(int foo=0; foo<num_surf_cells; foo++){
    cell_decided[foo] = false;
    flip_scheme[foo]=0;
  }
  //Choose a cell, grow a patch until no neighboring cells align well, then grow another patch.
  for(int foo=0; foo<num_surf_cells; foo++){
    if( !cell_decided[foo] ){
      patch_count++;
      cell_que.push( cellPrior{foo,1} );
      while(!cell_que.empty()){
        cellPrior current = cell_que.top();
        cell_que.pop();
        cell_counter++;
        cell_order->SetTuple1( current.id, cell_counter);
        if(!cell_decided[current.id]){
          if (current.confidence < 0) flip_scheme[current.id]=1;
          alligned_patch->SetTuple1(current.id, patch_count);
          cell_decided[current.id]=true;
          //Look for neighbors
          std::vector<int> ngbs = ngb_triangles(current.id , surface);
          for( int cell_ngb : ngbs ){

            double current_field[3];
            euc_field->GetTuple( current.id, current_field);
            vtkVector3d current_v(current_field);

            double ngb_field_euc[3];
            euc_field->GetTuple( cell_ngb, ngb_field_euc);
            vtkVector3d ngb_v(ngb_field_euc);

            //Prioritize based on the angle ~= alignment
            //and difference cuvature magnitudes ~= liklihood direction is correct

            double conf = current_v.Dot(ngb_v);
            if( fabs(conf) > INV_ROOT_2 ){
              double curvas[2];
              curvatures->GetTuple( cell_ngb, curvas );
              cellPrior get_in_line{ cell_ngb, 0};
              conf *= fabs(curvas[0]) - fabs(curvas[1]);
              if(current.confidence < 0) get_in_line.confidence = - conf;
              else get_in_line.confidence = conf;
              cell_que.push(get_in_line);
            }
          }
        }
      }
    }
  }
  std::cout<< patch_count <<std::endl;
  //Flip the cells that were antialigned
  for(int foo=0; foo< num_surf_cells; foo++){
    if ( flip_scheme[foo] == 1 ){
      double vv[3];
      euc_field->GetTuple( foo, vv);
      euc_field->SetTuple3( foo, -vv[0], -vv[1], -vv[2]);

      double uu[2];
      minor_curv_field->GetTuple( foo, uu);
      minor_curv_field->SetTuple2( foo, -uu[0], -uu[1]);
    }
  }
  //Align the patches starting with the largest patch.
  // bool patch_decided[patch_count];
  // for(int foo=0; foo<patch_count; foo++) patch_decided[foo]=false;
  // auto pt_to_max = std::max_element(patch_size.begin(), patch_size.end());
  // int big_patch = std::distance( patch_size.begin(), pt_to_max  );
  // std::queue<int> patch_que;
  // patch_que.push(big_patch);
  // while(!patch_que.empty()){
  //   int patch_adams = patch_que.front();
  //   patch_que.pop();
  //   std::cout<<patch_adams<<std::endl;
  //   if(!patch_decided[patch_adams]){
  //     patch_decided[patch_adams]=true;
  //     for(int elem: patch_to_nghbrs[patch_adams]){
  //       std::cout<<" "<<elem;
  //       patch_que.push(elem);
  //     }
  //     std::cout<<std::endl;
  //   }
  // }
  surface->GetCellData()->AddArray( cell_order );
  surface->GetCellData()->AddArray( alligned_patch );
  surface->GetCellData()->AddArray(minor_curv_field);
  surface->GetCellData()->AddArray(euc_field);
  return surface;
}




vtkSmartPointer<vtkPolyData> compute_curvature_frame(vtkSmartPointer<vtkPolyData> surface){
  vtkDataArray *normals=surface->GetCellData()->GetNormals();
  //Initialize memory for the minor curvautre field.
  //The field is defined per triangle of the surface.
  //The field is the direction of the shaper operator eignevalue with the lower
  //magintude curvature.
  //The field is expressed over in the barycentric coordinate basis
  // side2=p1-p0 and side1=p2-p0 for triangle /_p0p1p2
  auto minor_curv_field = vtkSmartPointer<vtkDoubleArray>::New();
  minor_curv_field->SetNumberOfComponents(2);
  minor_curv_field->SetNumberOfTuples(surface->GetNumberOfCells());
  minor_curv_field->SetName(minor_name);

  //The minor curvature field here is expressed over the standar basis in RR3
  auto euc_field = vtkSmartPointer<vtkDoubleArray>::New();
  euc_field->SetNumberOfComponents(3);
  euc_field->SetNumberOfTuples(surface->GetNumberOfCells());
  euc_field->SetName(euc_minor_name);
  //The major curvature field is orthogonal, completing the curvature frame
  auto curvatures = vtkSmartPointer<vtkDoubleArray>::New();
  curvatures->SetNumberOfComponents(2);
  curvatures->SetNumberOfTuples(surface->GetNumberOfCells());
  curvatures->SetName(curvatures_name);


  //Loop through the cells to compute the shape operator locally and extract
  //the minor curvature direction
  for(vtkIdType foo=0; foo< surface->GetNumberOfCells(); foo++){
    //Get the points as vectors
    double temp_pts[3][3];
    vtkVector3d pts[3];
    auto cell_pts = vtkSmartPointer<vtkIdList>::New();
    surface->GetCellPoints(foo,cell_pts);
    for(int bar=0;bar<3;bar++){
      surface->GetPoint(cell_pts->GetId(bar),temp_pts[bar]);
      pts[bar]=vtkVector3d(temp_pts[bar]);
    }
    //Get the normal vector
    double temp_n[3];
    normals->GetTuple(foo, temp_n);
    vtkVector3d normal(temp_n);
    //Compute_edge_vectors
    //For each edge compute the shape operator contribution.
    //The shape operator is discretized as
    // sum_{e in edges} (angle at edge) projection matrix on tangent perp to e
    // The shape operator is symmetric and computed in local edge coordinates.
    // Note that since we only care about direction, the shape operator is not
    // locally normalized so that the curvature is incorrect.
    double edge_lengths[3];
    vtkVector3d edges[3];// tang_vects[3];
    int edge_ends[3][3] = {{2,1},{0,2},{1,0}};
    double angles[3];
    //Compute the triangle edges
    for(int bar=0; bar<3; bar ++){
      //Edge vectors for orientation 012
      edges[bar] = pts [ edge_ends[bar][0] ] - pts [ edge_ends[bar][1] ];
      //Edge lengths
      edge_lengths[bar]=edges[bar].Norm();
      //Find neighboring cell across edge bar
    }
    //Compute a local orthonormal basis
    vtkVector3d orthobase0(edges[0]), orthobase1;
    orthobase0.Normalize();
    orthobase1 = normal.Cross(orthobase0);
    double shape00=0, shape01=0, shape11=0;
    for(int bar=0; bar<3; bar ++){
      //Find neighboring cell across edge bar
      auto cell_ngb = vtkSmartPointer<vtkIdList>::New();
      surface->GetCellEdgeNeighbors(foo,
        cell_pts->GetId(edge_ends[bar][0]),
        cell_pts->GetId(edge_ends[bar][1]),
        cell_ngb);
      //If there is no neighboring cell, i.e. the edge is surface boundary,
      //then we assume the surface is simply flat in that direction.
      //Otherwise add the angle scaled projection in that direction.
      if(cell_ngb->GetNumberOfIds()>0){
        normals->GetTuple(cell_ngb->GetId(0),temp_n);
        vtkVector3d ngb_normal(temp_n);
        //The sign of the angle is taken to agree with right-hand rotation
        //about the edge vector.
        double sine_of_ang = edges[bar].Dot( normal.Cross(ngb_normal) ) / edge_lengths[bar];
        if (sine_of_ang > 1) angles[bar] = M_PI/2;
        else if (sine_of_ang < -1) angles[bar] = -M_PI/2;
        else angles[bar] = asin( sine_of_ang ) ;
        if(isnan(angles[bar])) std::cout<<"NaN error! ";
        angles[bar] = angles[bar]/edge_lengths[bar];
        //Compute the shape operator matrix entries shape10=shape01
        //but except the eigenvalues are associated with the opposite eigenvectors
        //as in ~H of https://graphics.stanford.edu/courses/cs468-03-fall/Papers/cohen_normalcycle.pdf
        double dot0bar = orthobase0.Dot(edges[bar]);
        double dot1bar = orthobase1.Dot(edges[bar]);
        shape00 += angles[bar] * dot0bar * dot0bar;
        shape01 += angles[bar] * dot0bar * dot1bar;
        shape11 += angles[bar] * dot1bar * dot1bar;
      }
    }
  //Compute the minor eigenvalue and the corresponding eigenvector
    double shape_trace=shape00+shape11;
    double shape_discr=shape_trace*shape_trace-4*(shape00*shape11-shape01*shape01);
    if (shape_discr<0) std::cout<<"Contradiction! Shape operator eigs are complex!" <<foo;
    double major_curvature, minor_curvature;
    if(shape_trace>0){
      major_curvature=(shape_trace + sqrt(shape_discr))/2.0;
      minor_curvature=(shape_trace - sqrt(shape_discr))/2.0;
    }
    else{
      major_curvature=(shape_trace - sqrt(shape_discr))/2.0;
      minor_curvature=(shape_trace + sqrt(shape_discr))/2.0;
    }
    curvatures->SetTuple2(foo, major_curvature, minor_curvature);
    double ortho_curv_vect[2];
    if (fabs(major_curvature-shape00) > fabs(major_curvature-shape11)){
      ortho_curv_vect[0] = shape01;
      ortho_curv_vect[1] = major_curvature-shape00;
    }
    else{
      ortho_curv_vect[0] = major_curvature-shape11;
      ortho_curv_vect[1] = shape01;
    }
    vtkVector3d euc_curv_vect =  ortho_curv_vect[0] * orthobase0 + ortho_curv_vect[1] * orthobase1 ;
    double magni = euc_curv_vect.Norm();
    if (magni>0) euc_curv_vect =  1/magni * euc_curv_vect ;
    //Hacky attempt at semi continuity by choosing rando hemisphere to rep RealProjective2Space
    if(euc_curv_vect[2] < 0){
      euc_curv_vect = -1 * euc_curv_vect;
    }
    euc_field->SetTuple3( foo , euc_curv_vect[0], euc_curv_vect[1], euc_curv_vect[2]);
    //Also store the vector in the local edge basis e0=p2-p1 and p1=p0-p2
    double e00 = edges[0].Dot(edges[0]), e01 = edges[0].Dot(edges[1]), e11 = edges[1].Dot(edges[1]);
    double edet = e00*e11-e01*e01;
    double e0dw = edges[0].Dot(euc_curv_vect)  , e1dw = edges[1].Dot(euc_curv_vect) ;
    double minor_curv_vect[2] ={
      (e11*e0dw - e01 * e1dw) /edet,
      (e00*e1dw - e01 * e0dw) /edet,
    };
    minor_curv_field->SetTuple( foo , minor_curv_vect);
  }

  vtkVector3d dominant_dir={0,0,0};
  for(int foo=0; foo<surface->GetNumberOfCells(); foo++){
    double tempf[3];
    euc_field->GetTuple( foo , tempf);
    if ( (isnan(tempf[0])) || (isnan(tempf[0])) || (isnan(tempf[0]))){
      std::cout<< "NaN in " << foo << " " <<tempf[0] <<" "<<tempf[1] << " " << tempf[2];
    }
    vtkVector3d tempv(tempf);
    dominant_dir = dominant_dir + tempv;
  }
  std::cout << "Dominant direction " << dominant_dir[0] <<" " << dominant_dir[1] <<" " << dominant_dir[2] << std::endl;
  for(int foo=0; foo<surface->GetNumberOfCells(); foo++){
    double tempf[3];
    euc_field->GetTuple( foo , tempf);
    vtkVector3d tempv(tempf);
    if( dominant_dir.Dot(tempv) < 0){
      tempv= -1 * tempv;
      euc_field->SetTuple3( foo , tempv[0], tempv[1], tempv[2]);
      double templ[2];
      minor_curv_field->GetTuple(foo, templ);
      minor_curv_field->SetTuple2(foo, -templ[0], -templ[1]);
    }

  }
  //Looks noisey. Try a uniform window blur on cell neighbors.
  //There should be a fast way to do this with a convolution filter....
  // bool apply_blur = false;
  // if(apply_blur){
  //   const char* blur_name="BlurredField";
  //   auto blurred_field = vtkSmartPointer<vtkDoubleArray>::New();
  //   blurred_field->SetNumberOfComponents(3);
  //   blurred_field->SetNumberOfTuples(surface->GetNumberOfCells());
  //   blurred_field->SetName(blur_name);
  //   for(int foo = 0; foo<surface->GetNumberOfCells(); foo++){
  //     double tempf[3];
  //     euc_field->GetTuple( foo , tempf);
  //     vtkVector3d field_here(tempf);
  //     int edge_ends[3][3] = {{2,1},{0,2},{1,0}};
  //     auto cell_pts = vtkSmartPointer<vtkIdList>::New();
  //     surface->GetCellPoints(foo,cell_pts);
  //     auto cell_ngb = vtkSmartPointer<vtkIdList>::New();
  //     for(int bar=0;bar<3;bar++){
  //       surface->GetCellEdgeNeighbors(foo,
  //       cell_pts->GetId(edge_ends[bar][0]),
  //       cell_pts->GetId(edge_ends[bar][0]),
  //       cell_ngb);
  //       if(cell_ngb->GetNumberOfIds()>0){
  //         euc_field->GetTuple(cell_ngb->GetId(0), tempf);
  //         vtkVector3d field_there(tempf);
  //         field_here=field_here+field_there;
  //       }
  //     }
  //     field_here.Normalize();
  //     double * w = &field_here[0];
  //     blurred_field->SetTuple(foo, w);
  //     if (surface->GetCellData()->HasArray(blur_name))
  //       surface->GetCellData()->RemoveArray(blur_name);
  //     surface->GetCellData()->AddArray(blurred_field);
  //   }
  // }

  // If an old curvature array already exists we remove it


  if (surface->GetCellData()->HasArray(minor_name))
    surface->GetCellData()->RemoveArray(minor_name);
  if (surface->GetCellData()->HasArray(euc_minor_name))
    surface->GetCellData()->RemoveArray(euc_minor_name);
  if (surface->GetCellData()->HasArray(curvatures_name))
    surface->GetCellData()->RemoveArray(curvatures_name);

  surface->GetCellData()->AddArray(minor_curv_field);
  surface->GetCellData()->AddArray(euc_field);
  surface->GetCellData()->AddArray(curvatures);

  surface = allign_field(surface);

  return surface;
}


vtkSmartPointer<vtkPolyData> compute_tubular_parametrization(vtkSmartPointer<vtkPolyData> surface){
  //Contstruct a function u: {mesh points}->RR with gradient ~= minor curvature field
  int num_pts = surface->GetNumberOfPoints();
  auto cell_pts = vtkSmartPointer<vtkIdList>::New();
  int num_cells = surface->GetNumberOfCells();
  //Construct the discrete gradient matrix for the surface
  //The size is slightly larger to get constraint u(0)=0
  std::vector< Eigen::Triplet<double> > grad_entrylist;
  grad_entrylist.reserve(4 * num_cells+1);
  std::vector< Eigen::Triplet<double> > curv_entrylist;
  curv_entrylist.reserve(2 * num_cells+1);
  double curv_entry[2];
  vtkSmartPointer<vtkDataArray> curv_data = surface->GetCellData()->GetArray(minor_name);
  //Porting data from vtkArray to a Eigen::Matrix
  //This should probably be done when you compute it in the first place
  for(int foo=0; foo< num_cells; foo++){
    surface->GetCellPoints(foo,cell_pts);
    int pt0=cell_pts->GetId(0), pt1=cell_pts->GetId(1), pt2=cell_pts->GetId(2);
    //std::cout<<foo<<"cell's pts"<<pt0<<pt1<<pt2<<std::endl;
    curv_data->GetTuple(foo,curv_entry);
    double veccomp0 = curv_entry[0];
    double veccomp1 = curv_entry[1];
    double vec_norm = sqrt(veccomp0 * veccomp0 + veccomp1 * veccomp1);
    //This threshold for normalization is totally arbitrary
    //and should be informed by stats on the curvature.
    double normalizing_thresh = 1e-5;
    if (vec_norm > normalizing_thresh){
      veccomp0 *= 1/vec_norm;
      veccomp1 *= 1/vec_norm;
    }
    curv_entrylist.push_back( Eigen::Triplet<double> ( 2*foo, 0, veccomp0 ) );
    curv_entrylist.push_back( Eigen::Triplet<double> ( 2*foo+1, 0, veccomp1 ) );
    //The graditent per cell has component0 = u(p2)-u(p1)
    //and component1 = u(p0)-u(p2)
    grad_entrylist.push_back( Eigen::Triplet<double> (2*foo, pt2, 1) );
    grad_entrylist.push_back( Eigen::Triplet<double> (2*foo, pt1, -1) );
    grad_entrylist.push_back( Eigen::Triplet<double> (2*foo+1, pt0, 1) );
    grad_entrylist.push_back( Eigen::Triplet<double> (2*foo+1, pt2, -1) );
  }
  //Add constraint u(0)=0
  grad_entrylist.push_back( Eigen::Triplet<double> (2*num_cells, 0, 1) );
  curv_entrylist.push_back( Eigen::Triplet<double> (2*num_cells, 0, 0) );

  //Build Matrix
  Eigen::SparseMatrix<double> surf_gradient( 2*num_cells + 1, num_pts);
  surf_gradient.setFromTriplets(grad_entrylist.begin(), grad_entrylist.end());
  //Build target vector i.e. the curvature field
  Eigen::SparseMatrix<double> curv_field( 2*num_cells + 1, 1);
  curv_field.setFromTriplets(curv_entrylist.begin(), curv_entrylist.end());

  Eigen::LeastSquaresConjugateGradient< Eigen::SparseMatrix<double> > solver;
  std::cout<<"Factoring surface gradient."<<std::endl;
  solver.compute(surf_gradient);
  Eigen::SparseMatrix<double> u_param(num_pts,1);
  std::cout<<"Constructing tubular potential function."<<std::endl;
  time_t time1,time2;
  time1 = time(0);
  u_param = solver.solve(curv_field);
  time2 = time(0);
  int it_took = difftime(time2,time1);
  std::cout<<"Integration finished in " << it_took/60 << " min and " << it_took%60 << " sec." <<std::endl;
    std::cout<<"Required " << solver.iterations() << " iterations obtaining error " << solver.error() <<std::endl;
  bool verbose = false;
  const char* tube_param_name="Tubular Parametrization";
  auto tube_param = vtkSmartPointer<vtkDoubleArray>::New();
  tube_param->SetNumberOfComponents(1);
  tube_param->SetNumberOfTuples(num_pts);
  tube_param->SetName(tube_param_name);
  for(int foo=0; foo<num_pts; foo++){
    tube_param->SetTuple1(foo, u_param.coeffRef(foo,0));
  }
  surface->GetPointData()->AddArray(tube_param);
  return surface;
}

int main(int argc, const char* argv[]){
  const char* output_name;
  auto surface= vtkSmartPointer<vtkPolyData>::New();
  bool is_input = false;
  if (is_input) {
    if (argc < 3) {
      fprintf(stderr,"Usage: %s <input> [<output.vtp>] \n",argv[0]);
      return 0;
    }

    int length = strlen(argv[1]);
    const char* ext = argv[1] + length - 3;
    bool is_vtp = false;

    if (strcmp(ext,"obj") == 0) {
      vtkSmartPointer<vtkOBJReader> reader = vtkSmartPointer<vtkOBJReader>::New();
      reader->SetFileName(argv[1]);
      reader->Update();
      surface = reader->GetOutput();
    }
    else if (strcmp(ext,"stl") == 0) {
      vtkSmartPointer<vtkSTLReader> reader = vtkSmartPointer<vtkSTLReader>::New();
      reader->SetFileName(argv[1]);
      reader->Update();
      surface = reader->GetOutput();
    }
    else if (strcmp(ext,"vtp") == 0) {
      vtkSmartPointer<vtkXMLPolyDataReader> reader = vtkSmartPointer<vtkXMLPolyDataReader>::New();
      reader->SetFileName(argv[1]);
      reader->Update();
      surface = reader->GetOutput();

      is_vtp = true;
    }
    else {
      fprintf(stderr,"Unkown file format %s\n",ext);
      return 0;
    }

    // If we have no output name but the input file is not a vtp
    if ((argc < 3) && !is_vtp) {
      fprintf(stderr,"Error: For non vtp input files an output name is required\n");
      exit(0);
    }

    if (argc > 2)
      output_name = argv[2];
    else
      output_name = argv[1];
  }
  else{
    output_name = "bongo.vtp";
    int g;
    surface = build_small_simplicial_example();
  }

  // auto surface = vtkSmartPointer<vtkPolyData>::New();
  // vtkSmartPointer<vtkSTLReader> reader = vtkSmartPointer<vtkSTLReader>::New();
  // reader->SetFileName(argv[0]);
  // reader->Update();
  // surface = reader->GetOutput();
  //  surface->BuildCells();
  auto normalGenerator = vtkSmartPointer<vtkPolyDataNormals>::New();

  //Compute normal vectors for all cells
  std::cout<<"Computing normals."<<std::endl;
  normalGenerator->SetInputData(surface);
  normalGenerator->ComputePointNormalsOff();
  normalGenerator->ComputeCellNormalsOn();
  normalGenerator->SetSplitting(0);
  normalGenerator->AutoOrientNormalsOn();
  normalGenerator->Update();
  surface = normalGenerator->GetOutput();
  //surface->DeleteCells();
  surface->BuildLinks();
  std::cout<<"Computing minor principle curvature field. "<<std::endl;

  surface = compute_curvature_frame(surface);


  surface = compute_tubular_parametrization(surface);

  //File output
  std::cout<<"Writing file "<<output_name<<std::endl;
  vtkSmartPointer<vtkXMLPolyDataWriter> writer = vtkSmartPointer<vtkXMLPolyDataWriter>::New();
  writer->SetFileName(output_name);
  writer->SetInputData(surface);
  writer->Write();
}
