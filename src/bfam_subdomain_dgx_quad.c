#include <bfam_subdomain_dgx_quad.h>
#include <bfam_jacobi.h>
#include <bfam_kron.h>
#include <bfam_log.h>
#include <bfam_util.h>
#include <bfam_vtk.h>

static int
bfam_subdomain_dgx_quad_glue_vtk_write_vtu_piece(bfam_subdomain_t *subdomain,
    FILE *file, bfam_real_t time, const char **scalars, const char **vectors,
    const char **components, int writeBinary, int writeCompressed,
    int rank, bfam_locidx_t id, int Np_write)
{

  BFAM_ABORT_IF_NOT(Np_write == 0,
      "changing number of points not implemented for "
      "bfam_subdomain_dgx_quad_glue_t yet");
  bfam_subdomain_dgx_quad_glue_t *s =
    (bfam_subdomain_dgx_quad_glue_t*) subdomain;

  if(s->id_m != BFAM_MIN(s->id_m,s->id_p))
  {
    return 0;
  }

  const char *format;

  if(writeBinary)
    format = "binary";
  else
    format = "ascii";

  const bfam_locidx_t  K  = s->K;
  const int            N  = s->N;
  const int            Np = s->Np;

  const int Ncorners = s->Ncorners;

  const bfam_locidx_t Ncells = K * N;
  const bfam_locidx_t Ntotal = K * Np;

  bfam_real_t *restrict x =
    bfam_dictionary_get_value_ptr(&subdomain->fields, "_grid_x");
  bfam_real_t *restrict y =
    bfam_dictionary_get_value_ptr(&subdomain->fields, "_grid_y");
  bfam_real_t *restrict z =
    bfam_dictionary_get_value_ptr(&subdomain->fields, "_grid_z");

  fprintf(file,
           "    <Piece NumberOfPoints=\"%jd\" NumberOfCells=\"%jd\">\n",
           (intmax_t) Ntotal, (intmax_t) Ncells);

  /*
   * Points
   */
  fprintf (file, "      <Points>\n");

  bfam_vtk_write_real_vector_data_array(file, "Position", writeBinary,
      writeCompressed, Ntotal, x, y, z);

  fprintf(file, "      </Points>\n");

  /*
   * Cells
   */
  fprintf(file, "      <Cells>\n");

  /*
   * Connectivity
   */
  fprintf(file, "        <DataArray type=\"%s\" Name=\"connectivity\""
          " format=\"%s\">\n", BFAM_LOCIDX_VTK, format);
  if(writeBinary)
  {
    size_t cellsSize = Ncells*Ncorners*sizeof(bfam_locidx_t);
    bfam_locidx_t *cells = bfam_malloc_aligned(cellsSize);

    for(bfam_locidx_t k = 0, i = 0; k < K; ++k)
    {
      for(int m = 0; m < N; ++m)
      {
        cells[i++] = Np * k + (m + 0);
        cells[i++] = Np * k + (m + 1);
      }
    }

    fprintf(file, "          ");
    int rval =
      bfam_vtk_write_binary_data(writeCompressed, file, (char*)cells,
          cellsSize);
    fprintf(file, "\n");
    if(rval)
      BFAM_WARNING("Error encoding cells");

    bfam_free_aligned(cells);
  }
  else
  {
    for(bfam_locidx_t k = 0; k < K; ++k)
      for(int m = 0; m < N; ++m)
        fprintf(file,
                 "          %8jd %8jd\n",
                 (intmax_t) Np * k + (m + 0),
                 (intmax_t) Np * k + (m + 1));
  }
  fprintf(file, "        </DataArray>\n");

  /*
   * Offsets
   */
  fprintf (file, "        <DataArray type=\"%s\" Name=\"offsets\""
           " format=\"%s\">\n", BFAM_LOCIDX_VTK, format);
  fprintf(file, "          ");
  if(writeBinary)
  {
    size_t offsetsSize = Ncells*sizeof(bfam_locidx_t);
    bfam_locidx_t *offsets = bfam_malloc_aligned(offsetsSize);

    for(bfam_locidx_t i = 1; i <= Ncells; ++i)
      offsets[i - 1] = Ncorners * i;

    int rval =
      bfam_vtk_write_binary_data(writeCompressed, file, (char*)offsets,
          offsetsSize);
    if(rval)
      BFAM_WARNING("Error encoding offsets");

    bfam_free_aligned(offsets);
  }
  else
  {
    for(bfam_locidx_t i = 1, sk = 1; i <= Ncells; ++i, ++sk)
    {
      fprintf(file, " %8jd", (intmax_t) (Ncorners * i));
      if(!(sk % 20) && i != Ncells)
        fprintf(file, "\n          ");
    }
  }
  fprintf(file, "\n");
  fprintf(file, "        </DataArray>\n");

  /*
   * Types
   */
  fprintf(file, "        <DataArray type=\"UInt8\" Name=\"types\""
           " format=\"%s\">\n", format);
  fprintf(file, "          ");
  if(writeBinary)
  {
    size_t typesSize = Ncells*sizeof(uint8_t);
    uint8_t *types = bfam_malloc_aligned(typesSize);

    for(bfam_locidx_t i = 0; i < Ncells; ++i)
      types[i] = 3; /* VTK_LINE */

    int rval =
      bfam_vtk_write_binary_data(writeCompressed, file, (char*)types,
          typesSize);
    if(rval)
      BFAM_WARNING("Error encoding types");

    bfam_free_aligned(types);
  }
  else
  {
    for(bfam_locidx_t i = 0, sk = 1; i < Ncells; ++i, ++sk)
    {
      fprintf(file, " 3"); /* VTK_LINE */
      if (!(sk % 20) && i != (Ncells - 1))
        fprintf(file, "\n         ");
    }
  }
  fprintf(file, "\n");
  fprintf(file, "        </DataArray>\n");
  fprintf(file, "      </Cells>\n");

  /*
   * Cell Data
   */
  fprintf(file, "      <CellData Scalars=\"time,mpirank,subdomain_id\">\n");
  fprintf(file, "        <DataArray type=\"%s\" Name=\"time\""
           " format=\"%s\">\n", BFAM_REAL_VTK, format);
  fprintf(file, "          ");
  if(writeBinary)
  {
    size_t timesize = Ncells*sizeof(bfam_real_t);
    bfam_real_t *times = bfam_malloc_aligned(timesize);

    for(bfam_locidx_t i = 0; i < Ncells; ++i)
      times[i] = time;

    int rval =
      bfam_vtk_write_binary_data(writeCompressed, file, (char*)times,
          timesize);
    if(rval)
      BFAM_WARNING("Error encoding times");

    bfam_free_aligned(times);
  }
  else
  {
    for(bfam_locidx_t i = 0, sk = 1; i < Ncells; ++i, ++sk)
    {
      fprintf(file, " %"BFAM_REAL_FMTe, time);
      if (!(sk % 8) && i != (Ncells - 1))
        fprintf(file, "\n         ");
    }
  }
  fprintf(file, "\n");
  fprintf(file, "        </DataArray>\n");
  fprintf(file, "        <DataArray type=\"%s\" Name=\"mpirank\""
           " format=\"%s\">\n", BFAM_LOCIDX_VTK, format);
  fprintf(file, "          ");
  if(writeBinary)
  {
    size_t ranksSize = Ncells*sizeof(bfam_locidx_t);
    bfam_locidx_t *ranks = bfam_malloc_aligned(ranksSize);

    for(bfam_locidx_t i = 0; i < Ncells; ++i)
      ranks[i] = rank;

    int rval =
      bfam_vtk_write_binary_data(writeCompressed, file, (char*)ranks,
          ranksSize);
    if(rval)
      BFAM_WARNING("Error encoding ranks");

    bfam_free_aligned(ranks);
  }
  else
  {
    for(bfam_locidx_t i = 0, sk = 1; i < Ncells; ++i, ++sk)
    {
      fprintf(file, " %6jd", (intmax_t)rank);
      if (!(sk % 8) && i != (Ncells - 1))
        fprintf(file, "\n         ");
    }
  }
  fprintf(file, "\n");
  fprintf(file, "        </DataArray>\n");
  fprintf(file, "        <DataArray type=\"%s\" Name=\"subdomain_id\""
           " format=\"%s\">\n", BFAM_LOCIDX_VTK, format);
  fprintf(file, "          ");
  if(writeBinary)
  {
    size_t idsSize = Ncells*sizeof(bfam_locidx_t);
    bfam_locidx_t *ids = bfam_malloc_aligned(idsSize);

    for(bfam_locidx_t i = 0; i < Ncells; ++i)
      ids[i] = id;

    int rval =
      bfam_vtk_write_binary_data(writeCompressed, file, (char*)ids,
          idsSize);
    if(rval)
      BFAM_WARNING("Error encoding ids");

    bfam_free_aligned(ids);
  }
  else
  {
    for(bfam_locidx_t i = 0, sk = 1; i < Ncells; ++i, ++sk)
    {
      fprintf(file, " %6jd", (intmax_t)id);
      if (!(sk % 8) && i != (Ncells - 1))
        fprintf(file, "\n         ");
    }
  }
  fprintf(file, "\n");
  fprintf(file, "        </DataArray>\n");

  fprintf(file, "      </CellData>\n");

  char pointscalars[BFAM_BUFSIZ];
  bfam_util_strcsl(pointscalars, scalars);

  char pointvectors[BFAM_BUFSIZ];
  bfam_util_strcsl(pointvectors, vectors);

  fprintf(file, "      <PointData Scalars=\"%s\" Vectors=\"%s\">\n",
      pointscalars, pointvectors);

  if(scalars)
  {
    for(size_t s = 0; scalars[s]; ++s)
    {
      bfam_real_t *sdata = bfam_dictionary_get_value_ptr(&subdomain->fields,
          scalars[s]);
      BFAM_ABORT_IF(sdata == NULL, "VTK: Field %s not in subdomain %s",
          scalars[s], subdomain->name);
      bfam_vtk_write_real_scalar_data_array(file, scalars[s],
          writeBinary, writeCompressed, Ntotal, sdata);
    }
  }

  if(vectors)
  {
    for(size_t v = 0; vectors[v]; ++v)
    {

      bfam_real_t *v1 =
        bfam_dictionary_get_value_ptr(&subdomain->fields, components[3*v+0]);
      bfam_real_t *v2 =
        bfam_dictionary_get_value_ptr(&subdomain->fields, components[3*v+1]);
      bfam_real_t *v3 =
        bfam_dictionary_get_value_ptr(&subdomain->fields, components[3*v+2]);

      BFAM_ABORT_IF(v1 == NULL, "VTK: Field %s not in subdomain %s",
          components[3*v+0], subdomain->name);
      BFAM_ABORT_IF(v2 == NULL, "VTK: Field %s not in subdomain %s",
          components[3*v+1], subdomain->name);
      BFAM_ABORT_IF(v3 == NULL, "VTK: Field %s not in subdomain %s",
          components[3*v+2], subdomain->name);

      bfam_vtk_write_real_vector_data_array(file, vectors[v],
          writeBinary, writeCompressed, Ntotal, v1, v2, v3);
    }
  }

  fprintf(file, "      </PointData>\n");
  fprintf(file, "    </Piece>\n");

  return 1;
}

static void
bfam_subdomain_dgx_quad_vtk_interp(bfam_locidx_t K,
    int N_d,       bfam_real_t * restrict d,
    int N_s, const bfam_real_t * restrict s,
    const bfam_real_t *restrict interp)
{
  BFAM_ASSUME_ALIGNED(d,32);
  BFAM_ASSUME_ALIGNED(s,32);
  BFAM_ASSUME_ALIGNED(interp,32);
  for(int elem = 0; elem < K; elem++)
  {
    int o_d = elem * (N_d+1)*(N_d+1);
    int o_s = elem * (N_s+1)*(N_s+1);
    for(int n = 0; n < (N_d+1)*(N_d+1); n++) d[o_d+n] = 0;

    for(int l = 0; l < N_s+1; l++)
      for(int k = 0; k < N_s+1; k++)
        for(int j = 0; j < N_d+1; j++)
          for(int i = 0; i < N_d+1; i++)
            d[o_d+j*(N_d+1)+i] +=
              interp[(N_d+1)*l+j]*interp[(N_d+1)*k+i]
              *s[o_s+l*(N_s+1)+k];
  }
}

static int
bfam_subdomain_dgx_quad_vtk_write_vtu_piece(bfam_subdomain_t *subdomain,
    FILE *file, bfam_real_t time, const char **scalars, const char **vectors,
    const char **components, int writeBinary, int writeCompressed,
    int rank, bfam_locidx_t id, int Np_write)
{
  bfam_subdomain_dgx_quad_t *sub = (bfam_subdomain_dgx_quad_t*) subdomain;

  const char *format;

  if(writeBinary)
    format = "binary";
  else
    format = "ascii";

  const bfam_locidx_t  K  = sub->K;
  int            N_vtk  = sub->N;
  int            Np_vtk = sub->Np;
  bfam_real_t *interp = NULL;

  bfam_real_t *restrict stor1 = NULL;
  bfam_real_t *restrict stor2 = NULL;
  bfam_real_t *restrict stor3 = NULL;

  if(Np_write > 0)
  {
    BFAM_ABORT_IF_NOT(Np_write > 1, "Np_write = %d is not valid",
        Np_write);

    N_vtk  = Np_write - 1;
    Np_vtk = Np_write*Np_write;

    interp = bfam_malloc_aligned(sizeof(bfam_real_t)*(sub->N+1)*(N_vtk+1));

    bfam_long_real_t *calc_interp =
      bfam_malloc_aligned(sizeof(bfam_long_real_t)*(sub->N+1)*(N_vtk+1));
    bfam_long_real_t *lr =
      bfam_malloc_aligned(sizeof(bfam_long_real_t)*Np_write);

    for(int r = 0; r < Np_write; r++)
      lr[r] = -1 + 2*(bfam_long_real_t)r/(Np_write-1);

    bfam_jacobi_p_interpolation(0, 0, sub->N, Np_write, lr, sub->V,
        calc_interp);

    for(int n = 0; n < (sub->N+1)*(N_vtk+1); n++)
      interp[n] = (bfam_real_t)calc_interp[n];

    stor1 = bfam_malloc_aligned(sizeof(bfam_real_t)*Np_vtk*K);
    stor2 = bfam_malloc_aligned(sizeof(bfam_real_t)*Np_vtk*K);
    stor3 = bfam_malloc_aligned(sizeof(bfam_real_t)*Np_vtk*K);

    bfam_free_aligned(lr);
    bfam_free_aligned(calc_interp);
  }

  const int Ncorners = sub->Ncorners;

  const bfam_locidx_t Ncells = K * N_vtk * N_vtk;
  const bfam_locidx_t Ntotal = K * Np_vtk;

  bfam_real_t *restrict x =
    bfam_dictionary_get_value_ptr(&subdomain->fields, "_grid_x");
  bfam_real_t *restrict y =
    bfam_dictionary_get_value_ptr(&subdomain->fields, "_grid_y");
  bfam_real_t *restrict z =
    bfam_dictionary_get_value_ptr(&subdomain->fields, "_grid_z");

  if(interp == NULL)
  {
    stor1 = x;
    stor2 = y;
    stor3 = z;
  }
  else
  {
    bfam_subdomain_dgx_quad_vtk_interp(K,N_vtk,stor1,sub->N,x,interp);
    bfam_subdomain_dgx_quad_vtk_interp(K,N_vtk,stor2,sub->N,y,interp);
    bfam_subdomain_dgx_quad_vtk_interp(K,N_vtk,stor3,sub->N,z,interp);
  }

  fprintf(file,
           "    <Piece NumberOfPoints=\"%jd\" NumberOfCells=\"%jd\">\n",
           (intmax_t) Ntotal, (intmax_t) Ncells);

  /*
   * Points
   */
  fprintf (file, "      <Points>\n");

  bfam_vtk_write_real_vector_data_array(file, "Position", writeBinary,
      writeCompressed, Ntotal, stor1, stor2, stor3);

  fprintf(file, "      </Points>\n");

  /*
   * Cells
   */
  fprintf(file, "      <Cells>\n");

  /*
   * Connectivity
   */
  fprintf(file, "        <DataArray type=\"%s\" Name=\"connectivity\""
          " format=\"%s\">\n", BFAM_LOCIDX_VTK, format);
  if(writeBinary)
  {
    size_t cellsSize = Ncells*Ncorners*sizeof(bfam_locidx_t);
    bfam_locidx_t *cells = bfam_malloc_aligned(cellsSize);

    for(bfam_locidx_t k = 0, i = 0; k < K; ++k)
    {
      for(int m = 0; m < N_vtk; ++m)
      {
        for(int n = 0; n < N_vtk; ++n)
        {
          cells[i++] = Np_vtk * k + (N_vtk + 1) * (m + 0) + (n + 0);
          cells[i++] = Np_vtk * k + (N_vtk + 1) * (m + 0) + (n + 1);
          cells[i++] = Np_vtk * k + (N_vtk + 1) * (m + 1) + (n + 0);
          cells[i++] = Np_vtk * k + (N_vtk + 1) * (m + 1) + (n + 1);
        }
      }
    }

    fprintf(file, "          ");
    int rval =
      bfam_vtk_write_binary_data(writeCompressed, file, (char*)cells,
          cellsSize);
    fprintf(file, "\n");
    if(rval)
      BFAM_WARNING("Error encoding cells");

    bfam_free_aligned(cells);
  }
  else
  {
    for(bfam_locidx_t k = 0; k < K; ++k)
      for(int m = 0; m < N_vtk; ++m)
        for(int n = 0; n < N_vtk; ++n)
          fprintf(file,
                   "          %8jd %8jd %8jd %8jd\n",
                   (intmax_t) Np_vtk * k + (N_vtk + 1) * (m + 0) + (n + 0),
                   (intmax_t) Np_vtk * k + (N_vtk + 1) * (m + 0) + (n + 1),
                   (intmax_t) Np_vtk * k + (N_vtk + 1) * (m + 1) + (n + 0),
                   (intmax_t) Np_vtk * k + (N_vtk + 1) * (m + 1) + (n + 1));
  }
  fprintf(file, "        </DataArray>\n");

  /*
   * Offsets
   */
  fprintf (file, "        <DataArray type=\"%s\" Name=\"offsets\""
           " format=\"%s\">\n", BFAM_LOCIDX_VTK, format);
  fprintf(file, "          ");
  if(writeBinary)
  {
    size_t offsetsSize = Ncells*sizeof(bfam_locidx_t);
    bfam_locidx_t *offsets = bfam_malloc_aligned(offsetsSize);

    for(bfam_locidx_t i = 1; i <= Ncells; ++i)
      offsets[i - 1] = Ncorners * i;

    int rval =
      bfam_vtk_write_binary_data(writeCompressed, file, (char*)offsets,
          offsetsSize);
    if(rval)
      BFAM_WARNING("Error encoding offsets");

    bfam_free_aligned(offsets);
  }
  else
  {
    for(bfam_locidx_t i = 1, sk = 1; i <= Ncells; ++i, ++sk)
    {
      fprintf(file, " %8jd", (intmax_t) (Ncorners * i));
      if(!(sk % 20) && i != Ncells)
        fprintf(file, "\n          ");
    }
  }
  fprintf(file, "\n");
  fprintf(file, "        </DataArray>\n");

  /*
   * Types
   */
  fprintf(file, "        <DataArray type=\"UInt8\" Name=\"types\""
           " format=\"%s\">\n", format);
  fprintf(file, "          ");
  if(writeBinary)
  {
    size_t typesSize = Ncells*sizeof(uint8_t);
    uint8_t *types = bfam_malloc_aligned(typesSize);

    for(bfam_locidx_t i = 0; i < Ncells; ++i)
      types[i] = 8; /* VTK_PIXEL */

    int rval =
      bfam_vtk_write_binary_data(writeCompressed, file, (char*)types,
          typesSize);
    if(rval)
      BFAM_WARNING("Error encoding types");

    bfam_free_aligned(types);
  }
  else
  {
    for(bfam_locidx_t i = 0, sk = 1; i < Ncells; ++i, ++sk)
    {
      fprintf(file, " 8"); /* VTK_PIXEL */
      if (!(sk % 20) && i != (Ncells - 1))
        fprintf(file, "\n         ");
    }
  }
  fprintf(file, "\n");
  fprintf(file, "        </DataArray>\n");
  fprintf(file, "      </Cells>\n");

  /*
   * Cell Data
   */
  fprintf(file, "      <CellData Scalars=\"time,mpirank,subdomain_id\">\n");
  fprintf(file, "        <DataArray type=\"%s\" Name=\"time\""
           " format=\"%s\">\n", BFAM_REAL_VTK, format);
  fprintf(file, "          ");
  if(writeBinary)
  {
    size_t timesize = Ncells*sizeof(bfam_real_t);
    bfam_real_t *times = bfam_malloc_aligned(timesize);

    for(bfam_locidx_t i = 0; i < Ncells; ++i)
      times[i] = time;

    int rval =
      bfam_vtk_write_binary_data(writeCompressed, file, (char*)times,
          timesize);
    if(rval)
      BFAM_WARNING("Error encoding times");

    bfam_free_aligned(times);
  }
  else
  {
    for(bfam_locidx_t i = 0, sk = 1; i < Ncells; ++i, ++sk)
    {
      fprintf(file, " %"BFAM_REAL_FMTe, time);
      if (!(sk % 8) && i != (Ncells - 1))
        fprintf(file, "\n         ");
    }
  }
  fprintf(file, "\n");
  fprintf(file, "        </DataArray>\n");
  fprintf(file, "        <DataArray type=\"%s\" Name=\"mpirank\""
           " format=\"%s\">\n", BFAM_LOCIDX_VTK, format);
  fprintf(file, "          ");
  if(writeBinary)
  {
    size_t ranksSize = Ncells*sizeof(bfam_locidx_t);
    bfam_locidx_t *ranks = bfam_malloc_aligned(ranksSize);

    for(bfam_locidx_t i = 0; i < Ncells; ++i)
      ranks[i] = rank;

    int rval =
      bfam_vtk_write_binary_data(writeCompressed, file, (char*)ranks,
          ranksSize);
    if(rval)
      BFAM_WARNING("Error encoding ranks");

    bfam_free_aligned(ranks);
  }
  else
  {
    for(bfam_locidx_t i = 0, sk = 1; i < Ncells; ++i, ++sk)
    {
      fprintf(file, " %6jd", (intmax_t)rank);
      if (!(sk % 8) && i != (Ncells - 1))
        fprintf(file, "\n         ");
    }
  }
  fprintf(file, "\n");
  fprintf(file, "        </DataArray>\n");
  fprintf(file, "        <DataArray type=\"%s\" Name=\"subdomain_id\""
           " format=\"%s\">\n", BFAM_LOCIDX_VTK, format);
  fprintf(file, "          ");
  if(writeBinary)
  {
    size_t idsSize = Ncells*sizeof(bfam_locidx_t);
    bfam_locidx_t *ids = bfam_malloc_aligned(idsSize);

    for(bfam_locidx_t i = 0; i < Ncells; ++i)
      ids[i] = id;

    int rval =
      bfam_vtk_write_binary_data(writeCompressed, file, (char*)ids,
          idsSize);
    if(rval)
      BFAM_WARNING("Error encoding ids");

    bfam_free_aligned(ids);
  }
  else
  {
    for(bfam_locidx_t i = 0, sk = 1; i < Ncells; ++i, ++sk)
    {
      fprintf(file, " %6jd", (intmax_t)id);
      if (!(sk % 8) && i != (Ncells - 1))
        fprintf(file, "\n         ");
    }
  }
  fprintf(file, "\n");
  fprintf(file, "        </DataArray>\n");

  fprintf(file, "      </CellData>\n");

  char pointscalars[BFAM_BUFSIZ];
  bfam_util_strcsl(pointscalars, scalars);

  char pointvectors[BFAM_BUFSIZ];
  bfam_util_strcsl(pointvectors, vectors);

  fprintf(file, "      <PointData Scalars=\"%s\" Vectors=\"%s\">\n",
      pointscalars, pointvectors);

  if(scalars)
  {
    for(size_t s = 0; scalars[s]; ++s)
    {
      bfam_real_t *sdata = bfam_dictionary_get_value_ptr(&subdomain->fields,
          scalars[s]);
      BFAM_ABORT_IF(sdata == NULL, "VTK: Field %s not in subdomain %s",
          scalars[s], subdomain->name);
      if(interp == NULL)
      {
        stor1 = sdata;
      }
      else
      {
        bfam_subdomain_dgx_quad_vtk_interp(K,N_vtk,stor1,sub->N,sdata,interp);
      }

      bfam_vtk_write_real_scalar_data_array(file, scalars[s],
          writeBinary, writeCompressed, Ntotal, stor1);
    }
  }

  if(vectors)
  {
    for(size_t v = 0; vectors[v]; ++v)
    {

      bfam_real_t *v1 =
        bfam_dictionary_get_value_ptr(&subdomain->fields, components[3*v+0]);
      bfam_real_t *v2 =
        bfam_dictionary_get_value_ptr(&subdomain->fields, components[3*v+1]);
      bfam_real_t *v3 =
        bfam_dictionary_get_value_ptr(&subdomain->fields, components[3*v+2]);

      BFAM_ABORT_IF(v1 == NULL, "VTK: Field %s not in subdomain %s",
          components[3*v+0], subdomain->name);
      BFAM_ABORT_IF(v2 == NULL, "VTK: Field %s not in subdomain %s",
          components[3*v+1], subdomain->name);
      BFAM_ABORT_IF(v3 == NULL, "VTK: Field %s not in subdomain %s",
          components[3*v+2], subdomain->name);
      if(interp == NULL)
      {
        stor1 = v1;
        stor2 = v2;
        stor3 = v3;
      }
      else
      {
        bfam_subdomain_dgx_quad_vtk_interp(K,N_vtk,stor1,sub->N,v1,interp);
        bfam_subdomain_dgx_quad_vtk_interp(K,N_vtk,stor2,sub->N,v2,interp);
        bfam_subdomain_dgx_quad_vtk_interp(K,N_vtk,stor3,sub->N,v3,interp);
      }

      bfam_vtk_write_real_vector_data_array(file, vectors[v],
          writeBinary, writeCompressed, Ntotal, stor1, stor2, stor3);
    }
  }

  fprintf(file, "      </PointData>\n");
  fprintf(file, "    </Piece>\n");

  if(interp != NULL)
  {
    bfam_free_aligned(interp);
    bfam_free_aligned(stor1);
    bfam_free_aligned(stor2);
    bfam_free_aligned(stor3);
  }
  return 1;
}


bfam_subdomain_dgx_quad_t*
bfam_subdomain_dgx_quad_new(const bfam_locidx_t     id,
                            const char             *name,
                            const int               N,
                            const bfam_locidx_t     Nv,
                            const bfam_long_real_t *VX,
                            const bfam_long_real_t *VY,
                            const bfam_long_real_t *VZ,
                            const bfam_locidx_t     K,
                            const bfam_locidx_t    *EToV,
                            const bfam_locidx_t    *EToE,
                            const int8_t           *EToF)
{
  bfam_subdomain_dgx_quad_t* newSubdomain =
    bfam_malloc(sizeof(bfam_subdomain_dgx_quad_t));

  bfam_subdomain_dgx_quad_init(newSubdomain, id, name, N, Nv, VX, VY, VZ, K,
                               EToV, EToE, EToF);

  return newSubdomain;
}

static int
bfam_subdomain_dgx_quad_field_add(bfam_subdomain_t *subdomain, const char *name)
{
  bfam_subdomain_dgx_quad_t *s = (bfam_subdomain_dgx_quad_t*) subdomain;

  if(bfam_dictionary_get_value_ptr(&s->base.fields,name))
    return 1;

  size_t fieldSize = s->Np*s->K*sizeof(bfam_real_t);
  bfam_real_t *field = bfam_malloc_aligned(fieldSize);
#ifdef BFAM_DEBUG
  for(int i = 0; i < s->Np*s->K;i++) field[i] = bfam_real_nan("");
#endif

  int rval = bfam_dictionary_insert_ptr(&s->base.fields, name, field);

  BFAM_ASSERT(rval != 1);

  if(rval == 0)
    bfam_free_aligned(field);

  return rval;
}

static int
bfam_subdomain_dgx_quad_field_face_add(bfam_subdomain_t *subdomain,
    const char *name)
{
  bfam_subdomain_dgx_quad_t *s = (bfam_subdomain_dgx_quad_t*) subdomain;

  if(bfam_dictionary_get_value_ptr(&s->base.fields_face,name))
    return 1;

  size_t fieldSize = s->Nfaces*s->Nfp*s->K*sizeof(bfam_real_t);
  bfam_real_t *field = bfam_malloc_aligned(fieldSize);

  int rval = bfam_dictionary_insert_ptr(&s->base.fields_face, name, field);

  BFAM_ASSERT(rval != 1);

  if(rval == 0)
    bfam_free_aligned(field);

  return rval;
}

static void
bfam_subdomain_dgx_quad_field_init(bfam_subdomain_t *subdomain,
    const char *name, bfam_real_t time, bfam_subdomain_init_field_t init_field,
    void *arg)
{
  bfam_subdomain_dgx_quad_t *s = (bfam_subdomain_dgx_quad_t*) subdomain;

  bfam_real_t *field = bfam_dictionary_get_value_ptr(&s->base.fields,name);

  BFAM_ABORT_IF(field==NULL, "Init: Field %s not found in subdomain %s",
      name, subdomain->name);

  size_t fieldLength = s->Np*s->K;

  bfam_real_t *restrict x =
    bfam_dictionary_get_value_ptr(&subdomain->fields, "_grid_x");
  bfam_real_t *restrict y =
    bfam_dictionary_get_value_ptr(&subdomain->fields, "_grid_y");
  bfam_real_t *restrict z =
    bfam_dictionary_get_value_ptr(&subdomain->fields, "_grid_z");

  init_field(fieldLength, name, time, x, y, z, subdomain, arg, field);
}

static void
bfam_subdomain_dgx_quad_geo2D(int N, bfam_locidx_t K, int **fmask,
                              const bfam_long_real_t *restrict x,
                              const bfam_long_real_t *restrict y,
                              const bfam_long_real_t *restrict Dr,
                                    bfam_long_real_t *restrict Jrx,
                                    bfam_long_real_t *restrict Jsx,
                                    bfam_long_real_t *restrict Jry,
                                    bfam_long_real_t *restrict Jsy,
                                    bfam_long_real_t *restrict J,
                                    bfam_long_real_t *restrict nx,
                                    bfam_long_real_t *restrict ny,
                                    bfam_long_real_t *restrict sJ)
{
  const int Nfaces = 4;
  const int Nrp = N + 1;
  BFAM_ASSUME_ALIGNED( x, 32);
  BFAM_ASSUME_ALIGNED( y, 32);
  BFAM_ASSUME_ALIGNED(Dr, 32);
  BFAM_ASSUME_ALIGNED(Jrx, 32);
  BFAM_ASSUME_ALIGNED(Jsx, 32);
  BFAM_ASSUME_ALIGNED(Jry, 32);
  BFAM_ASSUME_ALIGNED(Jsy, 32);
  BFAM_ASSUME_ALIGNED( J, 32);

  for(bfam_locidx_t k = 0, vsk = 0, fsk = 0; k < K; ++k)
  {
    BFAM_KRON_IXA(Nrp, Dr, x + vsk, Jsy + vsk); /* xr */
    BFAM_KRON_IXA(Nrp, Dr, y + vsk, Jsx + vsk); /* yr */

    BFAM_KRON_AXI(Nrp, Dr, x + vsk, Jry + vsk); /* xs */
    BFAM_KRON_AXI(Nrp, Dr, y + vsk, Jrx + vsk); /* ys */

    for(int n = 0; n < Nrp*Nrp; ++n)
    {
      bfam_locidx_t idx = n + vsk;

      /* xr*ys - xs*yr */
      J[idx] = Jsy[idx]*Jrx[idx] - Jry[idx]*Jsx[idx];

      /* J*rx = ys */
      Jrx[idx] =  Jrx[idx];

      /* J*ry = -xs */
      Jry[idx] = -Jry[idx];

      /* J*sx = -yr */
      Jsx[idx] = -Jsx[idx];

      /* J*sy = xr */
      Jsy[idx] =  Jsy[idx];
    }

    for(int n = 0; n < Nrp; ++n)
    {
      const bfam_locidx_t fidx0 = fsk + 0 * Nrp + n;
      const bfam_locidx_t fidx1 = fsk + 1 * Nrp + n;
      const bfam_locidx_t fidx2 = fsk + 2 * Nrp + n;
      const bfam_locidx_t fidx3 = fsk + 3 * Nrp + n;

      const bfam_locidx_t vidx0 = vsk + fmask[0][n];
      const bfam_locidx_t vidx1 = vsk + fmask[1][n];
      const bfam_locidx_t vidx2 = vsk + fmask[2][n];
      const bfam_locidx_t vidx3 = vsk + fmask[3][n];

      /* face 0 */
      nx[fidx0] = -Jrx[vidx0]; /* -sy */
      ny[fidx0] = -Jry[vidx0]; /*  sx */

      /* face 1 */
      nx[fidx1] =  Jrx[vidx1]; /*  sy */
      ny[fidx1] =  Jry[vidx1]; /* -sx */

      /* face 2 */
      nx[fidx2] = -Jsx[vidx2]; /*  ry */
      ny[fidx2] = -Jsy[vidx2]; /* -rx */

      /* face 3 */
      nx[fidx3] =  Jsx[vidx3]; /* -sx */
      ny[fidx3] =  Jsy[vidx3]; /* -sx */

      sJ[fidx0] = BFAM_LONG_REAL_HYPOT(nx[fidx0],ny[fidx0]);
      sJ[fidx1] = BFAM_LONG_REAL_HYPOT(nx[fidx1],ny[fidx1]);
      sJ[fidx2] = BFAM_LONG_REAL_HYPOT(nx[fidx2],ny[fidx2]);
      sJ[fidx3] = BFAM_LONG_REAL_HYPOT(nx[fidx3],ny[fidx3]);

      nx[fidx0] /= sJ[fidx0];
      ny[fidx0] /= sJ[fidx0];
      nx[fidx1] /= sJ[fidx1];
      ny[fidx1] /= sJ[fidx1];
      nx[fidx2] /= sJ[fidx2];
      ny[fidx2] /= sJ[fidx2];
      nx[fidx3] /= sJ[fidx3];
      ny[fidx3] /= sJ[fidx3];
    }

    vsk += Nrp*Nrp;
    fsk += Nfaces*Nrp;
  }
}

static void
bfam_subdomain_dgx_quad_buildmaps(bfam_locidx_t K, int Np, int Nfp, int Nfaces,
   const bfam_locidx_t *EToE, const int8_t *EToF, int **fmask,
   bfam_locidx_t *restrict vmapP, bfam_locidx_t *restrict vmapM)
{
  for(bfam_locidx_t k1 = 0, sk = 0; k1 < K; ++k1)
  {
    for(int8_t f1 = 0; f1 < Nfaces; ++f1)
    {
      bfam_locidx_t k2 = EToE[Nfaces * k1 + f1];
      int8_t        f2 = EToF[Nfaces * k1 + f1] % Nfaces;
      int8_t        o  = EToF[Nfaces * k1 + f1] / Nfaces;

      for(int n = 0; n < Nfp; ++n)
      {
        vmapM[sk + n] = Np * k1 + fmask[f1][n];

        if(o)
          vmapP[sk + n] = Np * k2 + fmask[f2][Nfp-1-n];
        else
          vmapP[sk + n] = Np * k2 + fmask[f2][n];
      }

      sk += Nfp;
    }
  }
}

void
bfam_subdomain_dgx_quad_init(bfam_subdomain_dgx_quad_t       *subdomain,
                             const bfam_locidx_t              id,
                             const char                      *name,
                             const int                        N,
                             const bfam_locidx_t              Nv,
                             const bfam_long_real_t          *VX,
                             const bfam_long_real_t          *VY,
                             const bfam_long_real_t          *VZ,
                             const bfam_locidx_t              K,
                             const bfam_locidx_t             *EToV,
                             const bfam_locidx_t             *EToE,
                             const int8_t                    *EToF)
{
  bfam_subdomain_init(&subdomain->base, id, name);
  bfam_subdomain_add_tag(&subdomain->base, "_subdomain_dgx_quad");
  subdomain->base.free = bfam_subdomain_dgx_quad_free;
  subdomain->base.vtk_write_vtu_piece =
    bfam_subdomain_dgx_quad_vtk_write_vtu_piece;
  subdomain->base.field_add = bfam_subdomain_dgx_quad_field_add;
  subdomain->base.field_face_add = bfam_subdomain_dgx_quad_field_face_add;
  subdomain->base.field_init = bfam_subdomain_dgx_quad_field_init;

  const int Np = (N+1)*(N+1);
  const int Nfp = N+1;
  const int Nfaces = 4;
  const int Ncorners = 4;
  const int Nh = 3;
  const int No = 2;

  const int Nrp = N+1;

  subdomain->fmask = bfam_malloc_aligned(Nfaces * sizeof(int*));

  for(int f = 0; f < Nfaces; ++f)
    subdomain->fmask[f] = bfam_malloc_aligned(Nfp * sizeof(int));

  /*
   * Face 0 -x
   */
  for(int i = 0; i < N+1; ++i)
    subdomain->fmask[0][i] = i*(N+1);

  /*
   * Face 1 +x
   */
  for(int i = 0; i < N+1; ++i)
    subdomain->fmask[1][i] = (i+1)*(N+1)-1;

  /*
   * Face 2 -y
   */
  for(int i = 0; i < N+1; ++i)
    subdomain->fmask[2][i] = i;

  /*
   * Face 3 +y
   */
  for(int i = 0; i < N+1; ++i)
    subdomain->fmask[3][i] = (N+1)*N + i;


  bfam_long_real_t *lr, *lw;
  lr = bfam_malloc_aligned(Nrp*sizeof(bfam_long_real_t));
  lw = bfam_malloc_aligned(Nrp*sizeof(bfam_long_real_t));

  bfam_jacobi_gauss_lobatto_quadrature(0, 0, N, lr, lw);

  bfam_long_real_t *lx, *ly, *lz;
  lx = bfam_malloc_aligned(K*Np*sizeof(bfam_long_real_t));
  ly = bfam_malloc_aligned(K*Np*sizeof(bfam_long_real_t));
  lz = bfam_malloc_aligned(K*Np*sizeof(bfam_long_real_t));


  for(bfam_locidx_t k = 0; k < K; ++k)
  {
    bfam_locidx_t va = EToV[Ncorners * k + 0];
    bfam_locidx_t vb = EToV[Ncorners * k + 1];
    bfam_locidx_t vc = EToV[Ncorners * k + 2];
    bfam_locidx_t vd = EToV[Ncorners * k + 3];

    for(int n = 0; n < Nrp; ++n)
    {
      for(int m = 0; m < Nrp; ++m)
      {
        bfam_long_real_t wa, wb, wc, wd;

        wa = (1-lr[m])*(1-lr[n]);
        wb = (1+lr[m])*(1-lr[n]);
        wc = (1-lr[m])*(1+lr[n]);
        wd = (1+lr[m])*(1+lr[n]);

        lx[Np*k + Nrp*n + m] = (wa*VX[va]+wb*VX[vb]+wc*VX[vc]+wd*VX[vd])/4;
        ly[Np*k + Nrp*n + m] = (wa*VY[va]+wb*VY[vb]+wc*VY[vc]+wd*VY[vd])/4;
        lz[Np*k + Nrp*n + m] = (wa*VZ[va]+wb*VZ[vb]+wc*VZ[vc]+wd*VZ[vd])/4;
      }
    }
  }

  subdomain->V = bfam_malloc_aligned(Nrp*Nrp*sizeof(bfam_long_real_t));
  bfam_long_real_t *restrict V = subdomain->V;

  bfam_jacobi_p_vandermonde(0, 0, N, Nrp, lr, V);

  bfam_long_real_t *restrict D =
    bfam_malloc_aligned(Nrp*Nrp*sizeof(bfam_long_real_t));

  bfam_jacobi_p_differentiation(0, 0, N, Nrp, lr, V, D);

  bfam_long_real_t *restrict M =
    bfam_malloc_aligned(Nrp*Nrp*sizeof(bfam_long_real_t));

  bfam_jacobi_p_mass(0, 0, N, V, M);

  bfam_long_real_t *lJrx, *lJsx, *lJry, *lJsy, *lJ;

  lJrx = bfam_malloc_aligned(K*Np*sizeof(bfam_long_real_t));
  lJsx = bfam_malloc_aligned(K*Np*sizeof(bfam_long_real_t));
  lJry = bfam_malloc_aligned(K*Np*sizeof(bfam_long_real_t));
  lJsy = bfam_malloc_aligned(K*Np*sizeof(bfam_long_real_t));
  lJ  = bfam_malloc_aligned(K*Np*sizeof(bfam_long_real_t));

  bfam_long_real_t *lnx, *lny, *lsJ;

  lnx = bfam_malloc_aligned(K*Nfaces*Nfp*sizeof(bfam_long_real_t));
  lny = bfam_malloc_aligned(K*Nfaces*Nfp*sizeof(bfam_long_real_t));
  lsJ = bfam_malloc_aligned(K*Nfaces*Nfp*sizeof(bfam_long_real_t));

  bfam_subdomain_dgx_quad_geo2D(N, K, subdomain->fmask, lx, ly, D, lJrx, lJsx,
      lJry, lJsy, lJ, lnx, lny, lsJ);

  /*
   * Set subdomain values
   */
  subdomain->N = N;
  subdomain->Np = Np;
  subdomain->Nfp = Nfp;
  subdomain->Nfaces = Nfaces;
  subdomain->Ncorners = Ncorners;
  subdomain->Nh = Nh;
  subdomain->No = No;

  subdomain->r = bfam_malloc_aligned(Nrp*sizeof(bfam_real_t));
  subdomain->w = bfam_malloc_aligned(Nrp*sizeof(bfam_real_t));
  subdomain->wi = bfam_malloc_aligned(Nrp*sizeof(bfam_real_t));
  for(int n = 0; n<Nrp; ++n)
  {
    subdomain->r[n] = (bfam_real_t) lr[n];
    subdomain->w[n] = (bfam_real_t) lw[n];
    subdomain->wi[n] = 1.0/subdomain->w[n];
  }

  subdomain->K = K;

  int rval;
  rval = bfam_subdomain_dgx_quad_field_add(&subdomain->base, "_grid_x");
  BFAM_ABORT_IF_NOT(rval == 2, "Error adding _grid_x");
  rval = bfam_subdomain_dgx_quad_field_add(&subdomain->base, "_grid_y");
  BFAM_ABORT_IF_NOT(rval == 2, "Error adding _grid_y");
  rval = bfam_subdomain_dgx_quad_field_add(&subdomain->base, "_grid_z");
  BFAM_ABORT_IF_NOT(rval == 2, "Error adding _grid_z");

  rval = bfam_subdomain_dgx_quad_field_add(&subdomain->base, "_grid_Jrx");
  BFAM_ABORT_IF_NOT(rval == 2, "Error adding _grid_Jrx");
  rval = bfam_subdomain_dgx_quad_field_add(&subdomain->base, "_grid_Jry");
  BFAM_ABORT_IF_NOT(rval == 2, "Error adding _grid_Jry");
  rval = bfam_subdomain_dgx_quad_field_add(&subdomain->base, "_grid_Jsx");
  BFAM_ABORT_IF_NOT(rval == 2, "Error adding _grid_Jsx");
  rval = bfam_subdomain_dgx_quad_field_add(&subdomain->base, "_grid_Jsy");
  BFAM_ABORT_IF_NOT(rval == 2, "Error adding _grid_Jsy");

  rval = bfam_subdomain_dgx_quad_field_add(&subdomain->base, "_grid_J");
  BFAM_ABORT_IF_NOT(rval == 2, "Error adding _grid_J");
  rval = bfam_subdomain_dgx_quad_field_add(&subdomain->base, "_grid_JI");
  BFAM_ABORT_IF_NOT(rval == 2, "Error adding _grid_JI");

  bfam_real_t *restrict x =
    bfam_dictionary_get_value_ptr(&subdomain->base.fields, "_grid_x");
  bfam_real_t *restrict y =
    bfam_dictionary_get_value_ptr(&subdomain->base.fields, "_grid_y");
  bfam_real_t *restrict z =
    bfam_dictionary_get_value_ptr(&subdomain->base.fields, "_grid_z");

  bfam_real_t *restrict Jrx =
    bfam_dictionary_get_value_ptr(&subdomain->base.fields, "_grid_Jrx");
  bfam_real_t *restrict Jry =
    bfam_dictionary_get_value_ptr(&subdomain->base.fields, "_grid_Jry");
  bfam_real_t *restrict Jsx =
    bfam_dictionary_get_value_ptr(&subdomain->base.fields, "_grid_Jsx");
  bfam_real_t *restrict Jsy =
    bfam_dictionary_get_value_ptr(&subdomain->base.fields, "_grid_Jsy");

  bfam_real_t *restrict J =
    bfam_dictionary_get_value_ptr(&subdomain->base.fields, "_grid_J");
  bfam_real_t *restrict JI =
    bfam_dictionary_get_value_ptr(&subdomain->base.fields, "_grid_JI");

  for(int n = 0; n < K*Np; ++n)
  {
    x[n] = (bfam_real_t) lx[n];
    y[n] = (bfam_real_t) ly[n];
    z[n] = (bfam_real_t) lz[n];

    Jrx[n] = (bfam_real_t) lJrx[n];
    Jry[n] = (bfam_real_t) lJry[n];
    Jsx[n] = (bfam_real_t) lJsx[n];
    Jsy[n] = (bfam_real_t) lJsy[n];

    J[n]  = (bfam_real_t) lJ[n];
    JI[n] = (bfam_real_t) (BFAM_LONG_REAL(1.0)/lJ[n]);
  }

  rval = bfam_subdomain_dgx_quad_field_face_add(&subdomain->base, "_grid_nx");
  BFAM_ABORT_IF_NOT(rval == 2, "Error adding _grid_nx");
  rval = bfam_subdomain_dgx_quad_field_face_add(&subdomain->base, "_grid_ny");
  BFAM_ABORT_IF_NOT(rval == 2, "Error adding _grid_ny");
  rval = bfam_subdomain_dgx_quad_field_face_add(&subdomain->base, "_grid_sJ");
  BFAM_ABORT_IF_NOT(rval == 2, "Error adding _grid_sJ");

  bfam_real_t *restrict nx =
    bfam_dictionary_get_value_ptr(&subdomain->base.fields_face, "_grid_nx");
  bfam_real_t *restrict ny =
    bfam_dictionary_get_value_ptr(&subdomain->base.fields_face, "_grid_ny");
  bfam_real_t *restrict sJ =
    bfam_dictionary_get_value_ptr(&subdomain->base.fields_face, "_grid_sJ");

  for(int n = 0; n < K*Nfp*Nfaces; ++n)
  {
    nx[n] = (bfam_real_t) lnx[n];
    ny[n] = (bfam_real_t) lny[n];
    sJ[n] = (bfam_real_t) lsJ[n];
  }

  subdomain->Dr = bfam_malloc_aligned(Nrp * Nrp * sizeof(bfam_real_t));
  for(int n = 0; n < Nrp*Nrp; ++n)
    subdomain->Dr[n] = (bfam_real_t) D[n];

  subdomain->vmapP = bfam_malloc_aligned(K*Nfp*Nfaces*sizeof(bfam_locidx_t));
  subdomain->vmapM = bfam_malloc_aligned(K*Nfp*Nfaces*sizeof(bfam_locidx_t));

  bfam_subdomain_dgx_quad_buildmaps(K, Np, Nfp, Nfaces, EToE, EToF,
      subdomain->fmask, subdomain->vmapP, subdomain->vmapM);

  bfam_free_aligned(lnx);
  bfam_free_aligned(lny);
  bfam_free_aligned(lsJ);

  bfam_free_aligned(lJrx);
  bfam_free_aligned(lJsx);
  bfam_free_aligned(lJry);
  bfam_free_aligned(lJsy);
  bfam_free_aligned(lJ );

  bfam_free_aligned(D);
  bfam_free_aligned(M);

  bfam_free_aligned(lr);
  bfam_free_aligned(lw);

  bfam_free_aligned(lx);
  bfam_free_aligned(ly); bfam_free_aligned(lz);
}

static int
bfam_subdomain_dgx_quad_free_fields(const char * key, void *val,
    void *arg)
{
  bfam_free_aligned(val);

  return 1;
}

void
bfam_subdomain_dgx_quad_free(bfam_subdomain_t *thisSubdomain)
{
  bfam_subdomain_dgx_quad_t *sub = (bfam_subdomain_dgx_quad_t*) thisSubdomain;

  bfam_dictionary_allprefixed_ptr(&sub->base.fields,"",
      &bfam_subdomain_dgx_quad_free_fields,NULL);
  bfam_dictionary_allprefixed_ptr(&sub->base.fields_p,"",
      &bfam_subdomain_dgx_quad_free_fields,NULL);
  bfam_dictionary_allprefixed_ptr(&sub->base.fields_m,"",
      &bfam_subdomain_dgx_quad_free_fields,NULL);
  bfam_dictionary_allprefixed_ptr(&sub->base.fields_face,"",
      &bfam_subdomain_dgx_quad_free_fields,NULL);


  bfam_subdomain_free(thisSubdomain);

  bfam_free_aligned(sub->Dr);
  bfam_free_aligned(sub->V);

  bfam_free_aligned(sub->r);
  bfam_free_aligned(sub->w);
  bfam_free_aligned(sub->wi);

  for(int f = 0; f < sub->Nfaces; ++f)
    bfam_free_aligned(sub->fmask[f]);
  bfam_free_aligned(sub->fmask);

  bfam_free_aligned(sub->vmapP);
  bfam_free_aligned(sub->vmapM);
}

static void
bfam_subdomain_dgx_quad_glue_field_init(bfam_subdomain_t *sub,
    const char *name, bfam_real_t time, bfam_subdomain_init_field_t init_field,
    void *arg)
{
  bfam_subdomain_dgx_quad_glue_t *s = (bfam_subdomain_dgx_quad_glue_t*) sub;

  bfam_real_t *field = bfam_dictionary_get_value_ptr(&s->base.fields,name);

  BFAM_ABORT_IF(field==NULL, "Init: Field %s not found in subdomain %s",
      name, sub->name);

  size_t fieldLength = s->Np*s->K;

  bfam_real_t *restrict x =
    bfam_dictionary_get_value_ptr(&sub->fields, "_grid_x");
  bfam_real_t *restrict y =
    bfam_dictionary_get_value_ptr(&sub->fields, "_grid_y");
  bfam_real_t *restrict z =
    bfam_dictionary_get_value_ptr(&sub->fields, "_grid_z");

  init_field(fieldLength, name, time, x, y, z, sub, arg, field);
}

static int
bfam_subdomain_dgx_quad_glue_field_add(bfam_subdomain_t *subdomain,
    const char *name)
{
  bfam_subdomain_dgx_quad_glue_t *s =
    (bfam_subdomain_dgx_quad_glue_t*) subdomain;

  if(bfam_dictionary_get_value_ptr(&s->base.fields,name))
    return 1;

  size_t fieldSize = s->Np*s->K*sizeof(bfam_real_t);
  bfam_real_t *field = bfam_malloc_aligned(fieldSize);
#ifdef BFAM_DEBUG
  for(int i = 0; i < s->Np*s->K;i++) field[i] = bfam_real_nan("");
#endif

  int rval = bfam_dictionary_insert_ptr(&s->base.fields, name, field);

  BFAM_ASSERT(rval != 1);

  if(rval == 0)
    bfam_free_aligned(field);

  return rval;
}

static int
bfam_subdomain_dgx_quad_glue_field_minus_add(bfam_subdomain_t *subdomain,
    const char *name)
{
  bfam_subdomain_dgx_quad_glue_t *s =
    (bfam_subdomain_dgx_quad_glue_t*) subdomain;

  if(bfam_dictionary_get_value_ptr(&s->base.fields_m,name))
    return 1;

  size_t fieldSize = s->Np*s->K*sizeof(bfam_real_t);
  bfam_real_t *field = bfam_malloc_aligned(fieldSize);
#ifdef BFAM_DEBUG
  for(int i = 0; i < s->Np*s->K;i++) field[i] = bfam_real_nan("");
#endif

  int rval = bfam_dictionary_insert_ptr(&s->base.fields_m, name, field);

  BFAM_ASSERT(rval != 1);

  if(rval == 0)
    bfam_free_aligned(field);

  return rval;
}

static int
bfam_subdomain_dgx_quad_glue_field_plus_add(bfam_subdomain_t *subdomain,
    const char *name)
{
  bfam_subdomain_dgx_quad_glue_t *s =
    (bfam_subdomain_dgx_quad_glue_t*) subdomain;

  if(bfam_dictionary_get_value_ptr(&s->base.fields_p,name))
    return 1;

  size_t fieldSize = s->Np*s->K*sizeof(bfam_real_t);
  bfam_real_t *field = bfam_malloc_aligned(fieldSize);
#ifdef BFAM_DEBUG
  for(int i = 0; i < s->Np*s->K;i++) field[i] = bfam_real_nan("");
#endif

  int rval = bfam_dictionary_insert_ptr(&s->base.fields_p, name, field);

  BFAM_ASSERT(rval != 1);

  if(rval == 0)
    bfam_free_aligned(field);

  return rval;
}

static void
bfam_subdomain_dgx_quad_glue_comm_info(bfam_subdomain_t *thisSubdomain,
    int *rank, bfam_locidx_t *sort, int num_sort,
    size_t *send_sz, size_t *recv_sz, void *comm_args)
{
  BFAM_ASSERT(num_sort > 1);
  bfam_subdomain_dgx_quad_glue_t *sub =
    (bfam_subdomain_dgx_quad_glue_t*) thisSubdomain;

  *rank     = sub->rank_p;
  sort[0] = sub->id_p; /* neighbor ID */
  sort[1] = sub->id_m; /* my ID */

  size_t send_num = sub->base.fields_m.num_entries * sub->K * sub->Np;
  size_t recv_num = sub->base.fields_p.num_entries * sub->K * sub->Np;

  if(comm_args != NULL)
  {
    bfam_subdomain_comm_args_t *args = (bfam_subdomain_comm_args_t*) comm_args;

    int count = 0;
    for(int i = 0; args->scalars_m[i] != NULL;i++) count++;
    for(int i = 0; args->vectors_m[i] != NULL;i++) count+=4;
    for(int i = 0; args->tensors_m[i] != NULL;i++) count+=4;
    send_num = count * sub->K * sub->Np;

    count = 0;
    for(int i = 0; args->scalars_p[i] != NULL;i++) count++;
    for(int i = 0; args->vectors_p[i] != NULL;i++) count+=4;
    for(int i = 0; args->tensors_p[i] != NULL;i++) count+=4;
    recv_num = count * sub->K * sub->Np;
  }

  *send_sz  = send_num*sizeof(bfam_real_t);
  *recv_sz  = recv_num*sizeof(bfam_real_t);

  BFAM_LDEBUG(" rank %3d   ms %3jd   ns %3jd   send_sz %3zd   recv_sz %3zd",
      *rank, (intmax_t) sort[1], (intmax_t) sort[0], *send_sz, *recv_sz);
}

typedef struct bfam_subdomain_dgx_quad_get_put_data
{
  bfam_subdomain_dgx_quad_glue_t *sub;
  bfam_real_t *buffer;
  size_t size;
  size_t field;
} bfam_subdomain_dgx_quad_get_put_data_t;

static int
bfam_subdomain_dgx_quad_glue_get_vector_fields_m(const char **comp,
    void *vn, void *vp1, void *vp2, void *vp3, void *arg)
{
  BFAM_ASSUME_ALIGNED(vn, 32);
  BFAM_ASSUME_ALIGNED(vp1, 32);
  BFAM_ASSUME_ALIGNED(vp2, 32);
  BFAM_ASSUME_ALIGNED(vp3, 32);

  bfam_subdomain_dgx_quad_get_put_data_t *data =
    (bfam_subdomain_dgx_quad_get_put_data_t*) arg;

  bfam_subdomain_dgx_quad_glue_t *sub = data->sub;
  const bfam_locidx_t K = sub->K;
  const int Np = sub->Np;

  const bfam_locidx_t *restrict EToEp = sub->EToEp;
  const bfam_locidx_t *restrict EToEm = sub->EToEm;
  const int8_t        *restrict EToFm = sub->EToFm;
  const int8_t        *restrict EToHm = sub->EToHm;
  const int8_t        *restrict EToOm = sub->EToOm;
  BFAM_ASSUME_ALIGNED(EToEp, 32);
  BFAM_ASSUME_ALIGNED(EToEm, 32);
  BFAM_ASSUME_ALIGNED(EToFm, 32);
  BFAM_ASSUME_ALIGNED(EToHm, 32);
  BFAM_ASSUME_ALIGNED(EToOm, 32);


  const int sub_m_Np  = sub->sub_m->Np;
  const int sub_m_Nfp = sub->sub_m->Nfp;

  BFAM_ASSERT((data->field+4) * Np * K * sizeof(bfam_real_t) <= data->size);

  const bfam_real_t *restrict v1 =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields, comp[0]);
  BFAM_ASSERT(v1 != NULL);
  BFAM_ASSUME_ALIGNED(v1, 32);

  const bfam_real_t *restrict v2 =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields, comp[1]);
  BFAM_ASSERT(v2 != NULL);
  BFAM_ASSUME_ALIGNED(v2, 32);

  const bfam_real_t *restrict v3 =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields, comp[2]);
  BFAM_ASSERT(v3 != NULL);
  BFAM_ASSUME_ALIGNED(v3, 32);

  bfam_real_t *restrict n1 =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields_face, "_grid_nx");
  BFAM_ASSERT(n1 != NULL);
  BFAM_ASSUME_ALIGNED(n1, 32);

  bfam_real_t *restrict n2 =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields_face, "_grid_ny");
  BFAM_ASSERT(n2 != NULL);
  BFAM_ASSUME_ALIGNED(n2, 32);

  /*
  bfam_real_t *restrict n3 =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields_face, "_grid_nz");
  BFAM_ASSERT(n3 != NULL);
  BFAM_ASSUME_ALIGNED(n3, 32);
  */

  bfam_real_t *restrict sJ =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields_face, "_grid_sJ");
  BFAM_ASSERT(sJ != NULL);
  BFAM_ASSUME_ALIGNED(sJ, 32);

  /* Copy to the buffers */
  const size_t buffer_offset = data->field * Np * K;

  bfam_real_t *restrict send_vn  = data->buffer + buffer_offset + 0*Np*K;
  BFAM_ASSERT( send_vn != NULL);

  bfam_real_t *restrict send_vp1 = data->buffer + buffer_offset + 1*Np*K;
  BFAM_ASSERT( send_vp1 != NULL);

  bfam_real_t *restrict send_vp2 = data->buffer + buffer_offset + 2*Np*K;
  BFAM_ASSERT( send_vp2 != NULL);

  bfam_real_t *restrict send_vp3 = data->buffer + buffer_offset + 3*Np*K;
  BFAM_ASSERT( send_vp3 != NULL);

  for(bfam_locidx_t k = 0; k < K; ++k)
  {
    BFAM_ASSERT(EToEp[k] < sub->K);
    BFAM_ASSERT(EToEm[k] < sub->sub_m->K);
    BFAM_ASSERT(EToFm[k] < sub->sub_m->Nfaces);
    BFAM_ASSERT(EToHm[k] < sub->sub_m->Nh);
    BFAM_ASSERT(EToOm[k] < sub->sub_m->No);

    bfam_real_t *restrict vn_s_elem  = send_vn  + EToEp[k] * Np;
    bfam_real_t *restrict vp1_s_elem = send_vp1 + EToEp[k] * Np;
    bfam_real_t *restrict vp2_s_elem = send_vp2 + EToEp[k] * Np;
    bfam_real_t *restrict vp3_s_elem = send_vp3 + EToEp[k] * Np;

    int8_t face = EToFm[k];

    bfam_locidx_t *restrict fmask = sub->sub_m->fmask[face];

    const bfam_real_t *restrict v1_m_elem = v1 + EToEm[k] * sub_m_Np;
    const bfam_real_t *restrict v2_m_elem = v2 + EToEm[k] * sub_m_Np;
    const bfam_real_t *restrict v3_m_elem = v3 + EToEm[k] * sub_m_Np;

    bfam_real_t *restrict vn_g_elem  = (bfam_real_t*)vn  + k * Np;
    bfam_real_t *restrict vp1_g_elem = (bfam_real_t*)vp1 + k * Np;
    bfam_real_t *restrict vp2_g_elem = (bfam_real_t*)vp2 + k * Np;
    bfam_real_t *restrict vp3_g_elem = (bfam_real_t*)vp3 + k * Np;

    /*
     * Decide which interpolation operation to use.
     */
    const bfam_real_t *restrict interpolation = sub->interpolation[EToHm[k]];
    BFAM_ASSUME_ALIGNED(interpolation, 32);

    /*
     * Interpolate.
     */
    if(interpolation)
    {
      /*
       * XXX: Replace with something faster; this will also have to change
       * for 3D.
       */
      for(int n = 0; n < Np; ++n)
      {
        vn_g_elem[n]  = 0;
        vp1_g_elem[n] = 0;
        vp2_g_elem[n] = 0;
        vp3_g_elem[n] = 0;
      }
      for(int j = 0; j < sub_m_Nfp; ++j)
      {
        const bfam_locidx_t f   = j + sub_m_Nfp*(face + 4*EToEm[k]);
        const bfam_real_t sq_sJ = BFAM_REAL_SQRT(sJ[f]);
        const bfam_real_t vn_e  =       (n1[f]*v1_m_elem[fmask[j]]
                                       + n2[f]*v2_m_elem[fmask[j]]
                                     /*+ n3[f]*v3_m_elem[fmask[j]]*/);
        const bfam_real_t vp1_e =       v1_m_elem[fmask[j]]-vn_e*n1[f];
        const bfam_real_t vp2_e =       v2_m_elem[fmask[j]]-vn_e*n2[f];
        const bfam_real_t vp3_e =       v3_m_elem[fmask[j]]/*-vn_e*n3[f]*/;
        for(int i = 0; i < Np; ++i)
        {
          vn_g_elem[i]  += interpolation[j * Np + i] * vn_e;
          vp1_g_elem[i] += interpolation[j * Np + i] * vp1_e;
          vp2_g_elem[i] += interpolation[j * Np + i] * vp2_e;
          vp3_g_elem[i] += interpolation[j * Np + i] * vp3_e;
        }
      }
    }
    else
    {
      for(int j = 0; j < sub_m_Nfp; ++j)
      {
        const bfam_locidx_t f   = j + sub_m_Nfp*(face + 4*EToEm[k]);
        const bfam_real_t sq_sJ = BFAM_REAL_SQRT(sJ[f]);
        vn_g_elem[j]  =       (n1[f]*v1_m_elem[fmask[j]]
                             + n2[f]*v2_m_elem[fmask[j]]
                           /*+ n3[f]*v3_m_elem[fmask[j]]*/);
        vp1_g_elem[j] =       v1_m_elem[fmask[j]]-vn_g_elem[j]*n1[f];
        vp2_g_elem[j] =       v2_m_elem[fmask[j]]-vn_g_elem[j]*n2[f];
        vp3_g_elem[j] =       v3_m_elem[fmask[j]]/*-vn_g_elem[j]*n3[f]*/;
      }
    }

    /*
     * Copy data to send buffer based on orientation.
     */
    if(EToOm[k])
    {
      for(int n = 0; n < Np; ++n)
      {
        vn_s_elem[n]  = vn_g_elem[Np-1-n];
        vp1_s_elem[n] = vp1_g_elem[Np-1-n];
        vp2_s_elem[n] = vp2_g_elem[Np-1-n];
        vp3_s_elem[n] = vp3_g_elem[Np-1-n];
      }
    }
    else
    {
      memcpy(vn_s_elem,  vn_g_elem,  Np * sizeof(bfam_real_t));
      memcpy(vp1_s_elem, vp1_g_elem, Np * sizeof(bfam_real_t));
      memcpy(vp2_s_elem, vp2_g_elem, Np * sizeof(bfam_real_t));
      memcpy(vp3_s_elem, vp3_g_elem, Np * sizeof(bfam_real_t));
    }
  }

  data->field += 4;
  return 0;
}

static int
bfam_subdomain_dgx_quad_glue_get_tensor_fields_m(const char **comp,
    void *Tn, void *Tp1, void *Tp2, void *Tp3, void *arg)
{
  BFAM_ASSUME_ALIGNED(Tn, 32);
  BFAM_ASSUME_ALIGNED(Tp1, 32);
  BFAM_ASSUME_ALIGNED(Tp2, 32);
  BFAM_ASSUME_ALIGNED(Tp3, 32);

  bfam_subdomain_dgx_quad_get_put_data_t *data =
    (bfam_subdomain_dgx_quad_get_put_data_t*) arg;

  bfam_subdomain_dgx_quad_glue_t *sub = data->sub;
  const bfam_locidx_t K = sub->K;
  const int Np = sub->Np;

  const bfam_locidx_t *restrict EToEp = sub->EToEp;
  const bfam_locidx_t *restrict EToEm = sub->EToEm;
  const int8_t        *restrict EToFm = sub->EToFm;
  const int8_t        *restrict EToHm = sub->EToHm;
  const int8_t        *restrict EToOm = sub->EToOm;
  BFAM_ASSUME_ALIGNED(EToEp, 32);
  BFAM_ASSUME_ALIGNED(EToEm, 32);
  BFAM_ASSUME_ALIGNED(EToFm, 32);
  BFAM_ASSUME_ALIGNED(EToHm, 32);
  BFAM_ASSUME_ALIGNED(EToOm, 32);


  const int sub_m_Np  = sub->sub_m->Np;
  const int sub_m_Nfp = sub->sub_m->Nfp;

  BFAM_ASSERT((data->field+4) * Np * K * sizeof(bfam_real_t) <= data->size);

  const bfam_real_t *restrict S11 =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields, comp[0]);
  BFAM_ASSERT(S11 != NULL);
  BFAM_ASSUME_ALIGNED(S11, 32);

  const bfam_real_t *restrict S12 =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields, comp[1]);
  BFAM_ASSERT(S12 != NULL);
  BFAM_ASSUME_ALIGNED(S12, 32);

  const bfam_real_t *restrict S13 =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields, comp[2]);
  BFAM_ASSERT(S13 != NULL);
  BFAM_ASSUME_ALIGNED(S13, 32);

  const bfam_real_t *restrict S22 =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields, comp[3]);
  BFAM_ASSERT(S22 != NULL);
  BFAM_ASSUME_ALIGNED(S22, 32);

  const bfam_real_t *restrict S23 =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields, comp[4]);
  BFAM_ASSERT(S23 != NULL);
  BFAM_ASSUME_ALIGNED(S23, 32);

  const bfam_real_t *restrict S33 =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields, comp[5]);
  BFAM_ASSERT(S33 != NULL);
  BFAM_ASSUME_ALIGNED(S33, 32);

  bfam_real_t *restrict n1 =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields_face, "_grid_nx");
  BFAM_ASSERT(n1 != NULL);
  BFAM_ASSUME_ALIGNED(n1, 32);

  bfam_real_t *restrict n2 =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields_face, "_grid_ny");
  BFAM_ASSERT(n2 != NULL);
  BFAM_ASSUME_ALIGNED(n2, 32);

  /*
  bfam_real_t *restrict n3 =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields_face, "_grid_nz");
  BFAM_ASSERT(n3 != NULL);
  BFAM_ASSUME_ALIGNED(n3, 32);
  */

  bfam_real_t *restrict sJ =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields_face, "_grid_sJ");
  BFAM_ASSERT(sJ != NULL);
  BFAM_ASSUME_ALIGNED(sJ, 32);

  /* Copy to the buffers */
  const size_t buffer_offset = data->field * Np * K;

  bfam_real_t *restrict send_Tn  = data->buffer + buffer_offset + 0*Np*K;
  BFAM_ASSERT( send_Tn != NULL);

  bfam_real_t *restrict send_Tp1 = data->buffer + buffer_offset + 1*Np*K;
  BFAM_ASSERT( send_Tp1 != NULL);

  bfam_real_t *restrict send_Tp2 = data->buffer + buffer_offset + 2*Np*K;
  BFAM_ASSERT( send_Tp2 != NULL);

  bfam_real_t *restrict send_Tp3 = data->buffer + buffer_offset + 3*Np*K;
  BFAM_ASSERT( send_Tp3 != NULL);

  for(bfam_locidx_t k = 0; k < K; ++k)
  {
    BFAM_ASSERT(EToEp[k] < sub->K);
    BFAM_ASSERT(EToEm[k] < sub->sub_m->K);
    BFAM_ASSERT(EToFm[k] < sub->sub_m->Nfaces);
    BFAM_ASSERT(EToHm[k] < sub->sub_m->Nh);
    BFAM_ASSERT(EToOm[k] < sub->sub_m->No);

    bfam_real_t *restrict Tn_s_elem  = send_Tn  + EToEp[k] * Np;
    bfam_real_t *restrict Tp1_s_elem = send_Tp1 + EToEp[k] * Np;
    bfam_real_t *restrict Tp2_s_elem = send_Tp2 + EToEp[k] * Np;
    bfam_real_t *restrict Tp3_s_elem = send_Tp3 + EToEp[k] * Np;

    int8_t face = EToFm[k];

    bfam_locidx_t *restrict fmask = sub->sub_m->fmask[face];

    const bfam_real_t *restrict S11_m_elem = S11 + EToEm[k] * sub_m_Np;
    const bfam_real_t *restrict S12_m_elem = S12 + EToEm[k] * sub_m_Np;
    const bfam_real_t *restrict S13_m_elem = S13 + EToEm[k] * sub_m_Np;
    const bfam_real_t *restrict S22_m_elem = S22 + EToEm[k] * sub_m_Np;
    const bfam_real_t *restrict S23_m_elem = S23 + EToEm[k] * sub_m_Np;
    const bfam_real_t *restrict S33_m_elem = S33 + EToEm[k] * sub_m_Np;

    bfam_real_t *restrict Tn_g_elem  = (bfam_real_t*)Tn  + k * Np;
    bfam_real_t *restrict Tp1_g_elem = (bfam_real_t*)Tp1 + k * Np;
    bfam_real_t *restrict Tp2_g_elem = (bfam_real_t*)Tp2 + k * Np;
    bfam_real_t *restrict Tp3_g_elem = (bfam_real_t*)Tp3 + k * Np;

    /*
     * Decide which interpolation operation to use.
     */
    const bfam_real_t *restrict interpolation = sub->interpolation[EToHm[k]];
    BFAM_ASSUME_ALIGNED(interpolation, 32);

    /*
     * Interpolate.
     */
    if(interpolation)
    {
      /*
       * XXX: Replace with something faster; this will also have to change
       * for 3D.
       */
      for(int n = 0; n < Np; ++n)
      {
        Tn_g_elem[n]  = 0;
        Tp1_g_elem[n] = 0;
        Tp2_g_elem[n] = 0;
        Tp3_g_elem[n] = 0;
      }
      for(int j = 0; j < sub_m_Nfp; ++j)
      {
        const bfam_locidx_t f   = j + sub_m_Nfp*(face + 4*EToEm[k]);
        const bfam_locidx_t fm  = fmask[j];
        const bfam_real_t sq_sJ = BFAM_REAL_SQRT(sJ[f]);
        bfam_real_t Tp1_e =
          S11_m_elem[fm]*n1[f] + S12_m_elem[fm]*n2[f] + S13_m_elem[fm]*0;
        bfam_real_t Tp2_e =
          S12_m_elem[fm]*n1[f] + S22_m_elem[fm]*n2[f] + S23_m_elem[fm]*0;
        bfam_real_t Tp3_e =
          S13_m_elem[fm]*n1[f] + S23_m_elem[fm]*n2[f] + S33_m_elem[fm]*0;
        const bfam_real_t Tn_e  =  n1[f]*Tp1_e + n2[f]*Tp2_e + 0*Tp3_e;
        Tp1_e -= n1[f]*Tn_e;
        Tp2_e -= n2[f]*Tn_e;
        Tp3_e -= 0*Tn_e;
        for(int i = 0; i < Np; ++i)
        {
          Tn_g_elem[i]  += interpolation[j * Np + i] * Tn_e;
          Tp1_g_elem[i] += interpolation[j * Np + i] * Tp1_e;
          Tp2_g_elem[i] += interpolation[j * Np + i] * Tp2_e;
          Tp3_g_elem[i] += interpolation[j * Np + i] * Tp3_e;
        }
      }
    }
    else
    {
      for(int j = 0; j < sub_m_Nfp; ++j)
      {
        const bfam_locidx_t f   = j + sub_m_Nfp*(face + 4*EToEm[k]);
        const bfam_locidx_t fm  = fmask[j];
        const bfam_real_t sq_sJ = BFAM_REAL_SQRT(sJ[f]);
        Tp1_g_elem[j] =
          S11_m_elem[fm]*n1[f] + S12_m_elem[fm]*n2[f] + S13_m_elem[fm]*0;
        Tp2_g_elem[j] =
          S12_m_elem[fm]*n1[f] + S22_m_elem[fm]*n2[f] + S23_m_elem[fm]*0;
        Tp3_g_elem[j] =
          S13_m_elem[fm]*n1[f] + S23_m_elem[fm]*n2[f] + S33_m_elem[fm]*0;
        Tn_g_elem[j]  =  n1[f]*Tp1_g_elem[j] + n2[f]*Tp2_g_elem[j]
                      + 0*Tp3_g_elem[j];
        Tp1_g_elem[j] -= n1[f]*Tn_g_elem[j];
        Tp2_g_elem[j] -= n2[f]*Tn_g_elem[j];
        Tp3_g_elem[j] -= 0*Tn_g_elem[j];
      }
    }

    /*
     * Copy data to send buffer based on orientation.
     */
    if(EToOm[k])
    {
      for(int n = 0; n < Np; ++n)
      {
        Tn_s_elem[n]  = Tn_g_elem[Np-1-n];
        Tp1_s_elem[n] = Tp1_g_elem[Np-1-n];
        Tp2_s_elem[n] = Tp2_g_elem[Np-1-n];
        Tp3_s_elem[n] = Tp3_g_elem[Np-1-n];
      }
    }
    else
    {
      memcpy(Tn_s_elem,  Tn_g_elem,  Np * sizeof(bfam_real_t));
      memcpy(Tp1_s_elem, Tp1_g_elem, Np * sizeof(bfam_real_t));
      memcpy(Tp2_s_elem, Tp2_g_elem, Np * sizeof(bfam_real_t));
      memcpy(Tp3_s_elem, Tp3_g_elem, Np * sizeof(bfam_real_t));
    }
  }

  data->field += 4;
  return 0;
}

static int
bfam_subdomain_dgx_quad_glue_get_scalar_fields_m(const char * key, void *val,
    void *arg)
{
  bfam_subdomain_dgx_quad_get_put_data_t *data =
    (bfam_subdomain_dgx_quad_get_put_data_t*) arg;

  bfam_subdomain_dgx_quad_glue_t *sub = data->sub;
  const bfam_locidx_t K = sub->K;
  const int Np = sub->Np;

  const bfam_locidx_t *restrict EToEp = sub->EToEp;
  const bfam_locidx_t *restrict EToEm = sub->EToEm;
  const int8_t        *restrict EToFm = sub->EToFm;
  const int8_t        *restrict EToHm = sub->EToHm;
  const int8_t        *restrict EToOm = sub->EToOm;

  const int sub_m_Np = sub->sub_m->Np;
  const int sub_m_Nfp = sub->sub_m->Nfp;

  const size_t buffer_offset = data->field * Np * K;

  bfam_real_t *restrict send_field = data->buffer + buffer_offset;

  BFAM_ASSERT((data->field+1) * Np * K * sizeof(bfam_real_t) <= data->size);

  const bfam_real_t *restrict sub_m_field =
    bfam_dictionary_get_value_ptr(&sub->sub_m->base.fields, key);

  bfam_real_t *restrict glue_field = val;

  BFAM_ASSERT( send_field != NULL);
  BFAM_ASSERT(sub_m_field != NULL);
  BFAM_ASSERT( glue_field != NULL);

  BFAM_ASSUME_ALIGNED(sub_m_field, 32);
  BFAM_ASSUME_ALIGNED( glue_field, 32);

  BFAM_ASSUME_ALIGNED(EToEp, 32);
  BFAM_ASSUME_ALIGNED(EToEm, 32);
  BFAM_ASSUME_ALIGNED(EToFm, 32);
  BFAM_ASSUME_ALIGNED(EToHm, 32);
  BFAM_ASSUME_ALIGNED(EToOm, 32);

  for(bfam_locidx_t k = 0; k < K; ++k)
  {
    BFAM_ASSERT(EToEp[k] < sub->K);
    BFAM_ASSERT(EToEm[k] < sub->sub_m->K);
    BFAM_ASSERT(EToFm[k] < sub->sub_m->Nfaces);
    BFAM_ASSERT(EToHm[k] < sub->sub_m->Nh);
    BFAM_ASSERT(EToOm[k] < sub->sub_m->No);

    bfam_real_t *restrict send_elem = send_field + EToEp[k] * Np;

    bfam_locidx_t *restrict fmask = sub->sub_m->fmask[EToFm[k]];

    const bfam_real_t *restrict sub_m_elem = sub_m_field + EToEm[k] * sub_m_Np;

    bfam_real_t *restrict glue_elem = glue_field + k * Np;

    /*
     * Decide which interpolation operation to use.
     */
    const bfam_real_t *restrict interpolation = sub->interpolation[EToHm[k]];
    BFAM_ASSUME_ALIGNED(interpolation, 32);

    /*
     * Interpolate.
     */
    if(interpolation)
    {
      /*
       * XXX: Replace with something faster; this will also have to change
       * for 3D.
       */
      for(int n = 0; n < Np; ++n)
        glue_elem[n] = 0;
      for(int j = 0; j < sub_m_Nfp; ++j)
        for(int i = 0; i < Np; ++i)
          glue_elem[i] += interpolation[j * Np + i] * sub_m_elem[fmask[j]];
    }
    else
    {
      for(int i = 0; i < Np; ++i)
        glue_elem[i] = sub_m_elem[fmask[i]];
    }

    /*
     * Copy data to send buffer based on orientation.
     */
    if(EToOm[k])
    {
      for(int n = 0; n < Np; ++n)
        send_elem[n] = glue_elem[Np-1-n];
    }
    else
    {
      memcpy(send_elem, glue_elem, Np * sizeof(bfam_real_t));
    }
  }

  ++data->field;
  return 0;
}

void
bfam_subdomain_dgx_quad_glue_put_send_buffer(bfam_subdomain_t *thisSubdomain,
    void *buffer, size_t send_sz, void *comm_args)
{
  bfam_subdomain_dgx_quad_get_put_data_t data;

  data.sub    = (bfam_subdomain_dgx_quad_glue_t*) thisSubdomain;
  data.buffer = (bfam_real_t*) buffer;
  data.size   = send_sz;
  data.field  = 0;

  if(comm_args == NULL)
  {
    BFAM_ASSERT(send_sz == data.sub->base.fields_m.num_entries * data.sub->K *
        data.sub->Np * sizeof(bfam_real_t));

    /*
     * Fill fields_m and the send buffer from sub_m.
     */
    bfam_dictionary_allprefixed_ptr(&data.sub->base.fields_m, "",
        &bfam_subdomain_dgx_quad_glue_get_scalar_fields_m, &data);
  }
  else
  {
    bfam_subdomain_comm_args_t *args = (bfam_subdomain_comm_args_t*) comm_args;
    for(int s = 0; args->scalars_m[s] != NULL;s++)
    {
      const char *key  = args->scalars_m[s];
      void *field = bfam_dictionary_get_value_ptr(&data.sub->base.fields_m,key);
      bfam_subdomain_dgx_quad_glue_get_scalar_fields_m(key,field,&data);
    }
    for(int v = 0; args->vectors_m[v] != NULL;v++)
    {
      const char *vec_prefix  = args->vectors_m[v];
      char str[BFAM_BUFSIZ];
      snprintf(str,BFAM_BUFSIZ,"%sn" ,vec_prefix);
      void *vn  = bfam_dictionary_get_value_ptr(&data.sub->base.fields_m,str);
      snprintf(str,BFAM_BUFSIZ,"%sp1",vec_prefix);
      void *vp1 = bfam_dictionary_get_value_ptr(&data.sub->base.fields_m,str);
      snprintf(str,BFAM_BUFSIZ,"%sp2",vec_prefix);
      void *vp2 = bfam_dictionary_get_value_ptr(&data.sub->base.fields_m,str);
      snprintf(str,BFAM_BUFSIZ,"%sp3",vec_prefix);
      void *vp3 = bfam_dictionary_get_value_ptr(&data.sub->base.fields_m,str);
      BFAM_ASSERT(vn != NULL && vp1 != NULL && vp2 != NULL && vp3 != NULL);
      const char **comps = args->vector_components_m + 3*v;
      bfam_subdomain_dgx_quad_glue_get_vector_fields_m(comps,vn,vp1,vp2,vp3,
          &data);
    }
    for(int t = 0; args->tensors_m[t] != NULL;t++)
    {
      const char *ten_prefix  = args->tensors_m[t];
      char str[BFAM_BUFSIZ];
      snprintf(str,BFAM_BUFSIZ,"%sn" ,ten_prefix);
      void *tn  = bfam_dictionary_get_value_ptr(&data.sub->base.fields_m,str);
      snprintf(str,BFAM_BUFSIZ,"%sp1",ten_prefix);
      void *tp1 = bfam_dictionary_get_value_ptr(&data.sub->base.fields_m,str);
      snprintf(str,BFAM_BUFSIZ,"%sp2",ten_prefix);
      void *tp2 = bfam_dictionary_get_value_ptr(&data.sub->base.fields_m,str);
      snprintf(str,BFAM_BUFSIZ,"%sp3",ten_prefix);
      void *tp3 = bfam_dictionary_get_value_ptr(&data.sub->base.fields_m,str);
      BFAM_ASSERT(tn != NULL && tp1 != NULL && tp2 != NULL && tp3 != NULL);
      const char **comps = args->tensor_components_m + 3*t;
      bfam_subdomain_dgx_quad_glue_get_tensor_fields_m(comps,tn,tp1,tp2,tp3,
          &data);
    }
  }
}

static int
bfam_subdomain_dgx_quad_glue_put_scalar_fields_p(const char * key, void *val,
    void *arg)
{
  bfam_subdomain_dgx_quad_get_put_data_t *data =
    (bfam_subdomain_dgx_quad_get_put_data_t*) arg;

  const bfam_locidx_t K = data->sub->K;
  const int Np = data->sub->Np;

  const size_t buffer_offset = data->field * Np * K;
  const bfam_real_t *restrict recv_field = data->buffer + buffer_offset;

  BFAM_ASSERT((data->field+1) * Np * K * sizeof(bfam_real_t) <= data->size);

  bfam_real_t *restrict glue_field = val;

  BFAM_ASSUME_ALIGNED(glue_field, 32);

  memcpy(glue_field, recv_field, K * Np * sizeof(bfam_real_t));

  ++data->field;
  return 0;
}

void
bfam_subdomain_dgx_quad_glue_get_recv_buffer(bfam_subdomain_t *thisSubdomain,
    void *buffer, size_t recv_sz, void* comm_args)
{
  bfam_subdomain_dgx_quad_get_put_data_t data;

  data.sub    = (bfam_subdomain_dgx_quad_glue_t*) thisSubdomain;
  data.buffer = (bfam_real_t*) buffer;
  data.size   = recv_sz;
  data.field  = 0;

  if(comm_args == NULL)
  {
    BFAM_ASSERT(recv_sz == data.sub->base.fields_p.num_entries * data.sub->K *
        data.sub->Np * sizeof(bfam_real_t));

    /*
     * Fill fields_p from the recv buffer.
     */
    bfam_dictionary_allprefixed_ptr(&data.sub->base.fields_p, "",
        &bfam_subdomain_dgx_quad_glue_put_scalar_fields_p, &data);

    BFAM_ASSERT(recv_sz == data.sub->base.fields_p.num_entries * data.sub->K *
        data.sub->Np * sizeof(bfam_real_t));
  }
  else
  {
    bfam_subdomain_comm_args_t *args = (bfam_subdomain_comm_args_t*) comm_args;
    for(int s = 0; args->scalars_p[s] != NULL;s++)
    {
      const char *key  = args->scalars_p[s];
      void *field = bfam_dictionary_get_value_ptr(&data.sub->base.fields_p,key);
      bfam_subdomain_dgx_quad_glue_put_scalar_fields_p(key,field,&data);
    }
    const char *vt_postfix[] = {"n","p1","p2","p3",NULL};
    for(int v = 0; args->vectors_p[v] != NULL;v++)
    {
      for(int c = 0; vt_postfix[c] != NULL;c++)
      {
        char key[BFAM_BUFSIZ];
        snprintf(key,BFAM_BUFSIZ,"%s%s" ,args->vectors_p[v],vt_postfix[c]);
        void *field  = bfam_dictionary_get_value_ptr(&data.sub->base.fields_p,
                                                     key);
        bfam_subdomain_dgx_quad_glue_put_scalar_fields_p(key,field,&data);
      }
    }
    for(int t = 0; args->tensors_p[t] != NULL;t++)
    {
      for(int c = 0; vt_postfix[c] != NULL;c++)
      {
        char key[BFAM_BUFSIZ];
        snprintf(key,BFAM_BUFSIZ,"%s%s" ,args->tensors_p[t],vt_postfix[c]);
        void *field  = bfam_dictionary_get_value_ptr(&data.sub->base.fields_p,
                                                     key);
        bfam_subdomain_dgx_quad_glue_put_scalar_fields_p(key,field,&data);
      }
    }
  }
}


bfam_subdomain_dgx_quad_glue_t*
bfam_subdomain_dgx_quad_glue_new(const bfam_locidx_t              id,
                                 const char                      *name,
                                 const int                        N_m,
                                 const int                        N_p,
                                 const bfam_locidx_t              rank_m,
                                 const bfam_locidx_t              rank_p,
                                 const bfam_locidx_t              id_m,
                                 const bfam_locidx_t              id_p,
                                 bfam_subdomain_dgx_quad_t       *sub_m,
                                 bfam_locidx_t                   *ktok_m,
                                 const bfam_locidx_t              K,
                                 bfam_subdomain_face_map_entry_t *mapping)
{
  bfam_subdomain_dgx_quad_glue_t* newSubdomain =
    bfam_malloc(sizeof(bfam_subdomain_dgx_quad_glue_t));

  bfam_subdomain_dgx_quad_glue_init(newSubdomain, id, name, N_m, N_p, rank_m,
      rank_p, id_m, id_p, sub_m, ktok_m, K, mapping);

  return newSubdomain;
}

void
bfam_subdomain_dgx_quad_glue_init(bfam_subdomain_dgx_quad_glue_t  *subdomain,
                                  const bfam_locidx_t              id,
                                  const char                      *name,
                                  const int                        N_m,
                                  const int                        N_p,
                                  const bfam_locidx_t              rank_m,
                                  const bfam_locidx_t              rank_p,
                                  const bfam_locidx_t              id_m,
                                  const bfam_locidx_t              id_p,
                                  bfam_subdomain_dgx_quad_t       *sub_m,
                                  bfam_locidx_t                   *ktok_m,
                                  const bfam_locidx_t              K,
                                  bfam_subdomain_face_map_entry_t *mapping)
{
  bfam_subdomain_init(&subdomain->base, id, name);
  bfam_subdomain_add_tag(&subdomain->base, "_subdomain_dgx_quad");
  subdomain->base.glue_comm_info = bfam_subdomain_dgx_quad_glue_comm_info;
  subdomain->base.glue_put_send_buffer =
    bfam_subdomain_dgx_quad_glue_put_send_buffer;
  subdomain->base.glue_get_recv_buffer =
    bfam_subdomain_dgx_quad_glue_get_recv_buffer;
    subdomain->base.field_add = bfam_subdomain_dgx_quad_glue_field_add;
  subdomain->base.field_init = bfam_subdomain_dgx_quad_glue_field_init;
  subdomain->base.field_minus_add =
    bfam_subdomain_dgx_quad_glue_field_minus_add;
  subdomain->base.field_plus_add = bfam_subdomain_dgx_quad_glue_field_plus_add;
  subdomain->base.free = bfam_subdomain_dgx_quad_glue_free;
  subdomain->base.vtk_write_vtu_piece =
    bfam_subdomain_dgx_quad_glue_vtk_write_vtu_piece;

  const int N   = BFAM_MAX(N_m, N_p);
  const int Nrp = N+1;
  const int sub_m_Nrp = N_m+1;

  const int num_interp = 3;
  const bfam_long_real_t projection_scale[3] = {BFAM_LONG_REAL(1.0),
                                                BFAM_LONG_REAL(0.5),
                                                BFAM_LONG_REAL(0.5)};

  bfam_long_real_t **lr;
  lr = bfam_malloc_aligned(num_interp*sizeof(bfam_long_real_t*));

  for(int i = 0; i < num_interp; ++i)
    lr[i] = bfam_malloc_aligned(Nrp*sizeof(bfam_long_real_t));

  bfam_long_real_t *restrict lw;
  lw = bfam_malloc_aligned(Nrp*sizeof(bfam_long_real_t));

  bfam_jacobi_gauss_lobatto_quadrature(0, 0, N, lr[0], lw);

  const bfam_long_real_t half = BFAM_LONG_REAL(0.5);
  for(int n = 0; n < Nrp; ++n)
  {
    lr[1][n] = -half + half * lr[0][n];
    lr[2][n] =  half + half * lr[0][n];
  }

  bfam_long_real_t *restrict sub_m_lr, *restrict sub_m_lw;
  sub_m_lr = bfam_malloc_aligned(sub_m_Nrp*sizeof(bfam_long_real_t));
  sub_m_lw = bfam_malloc_aligned(sub_m_Nrp*sizeof(bfam_long_real_t));

  bfam_jacobi_gauss_lobatto_quadrature(0, 0, N_m, sub_m_lr, sub_m_lw);

  bfam_long_real_t *restrict sub_m_V;
  sub_m_V = bfam_malloc_aligned(sub_m_Nrp*sub_m_Nrp*sizeof(bfam_long_real_t));

  bfam_jacobi_p_vandermonde(0, 0, N_m, N_m+1, sub_m_lr, sub_m_V);

  bfam_long_real_t **interpolation =
    bfam_malloc_aligned(num_interp*sizeof(bfam_long_real_t*));

  for(int i = 0; i < num_interp; ++i)
  {
    interpolation[i] =
      bfam_malloc_aligned(Nrp*sub_m_Nrp*sizeof(bfam_long_real_t));

    bfam_jacobi_p_interpolation(0, 0, N_m, Nrp, lr[i], sub_m_V,
        interpolation[i]);
  }

  bfam_long_real_t **massprojection =
    bfam_malloc_aligned(num_interp*sizeof(bfam_long_real_t*));

  bfam_long_real_t *restrict V;
  V = bfam_malloc_aligned(Nrp*Nrp*sizeof(bfam_long_real_t));
  bfam_jacobi_p_vandermonde(0, 0, N, N+1, lr[0], V);

  bfam_long_real_t *restrict mass =
      bfam_malloc_aligned(Nrp*Nrp*sizeof(bfam_long_real_t));
  bfam_jacobi_p_mass(0, 0, N, V, mass);

  for(int i = 0; i < num_interp; ++i)
  {
    massprojection[i] =
      bfam_malloc_aligned(Nrp*sub_m_Nrp*sizeof(bfam_long_real_t));
    for(int n = 0; n < Nrp*sub_m_Nrp; n++) massprojection[i][n] = 0;

    bfam_util_mTmmult(sub_m_Nrp, Nrp, Nrp, interpolation[i], Nrp,
        mass, Nrp, massprojection[i], sub_m_Nrp);
    for(int n = 0; n < Nrp*sub_m_Nrp; n++)
      massprojection[i][n] *= projection_scale[i];
  }


  subdomain->N_m = N_m;
  subdomain->N_p = N_p;

  subdomain->rank_m = rank_m;
  subdomain->rank_p = rank_p;

  subdomain->id_m = id_m;
  subdomain->id_p = id_p;

  subdomain->s_m = imaxabs(id_m)-1;
  subdomain->s_p = imaxabs(id_p)-1;

  subdomain->sub_m = sub_m;

  subdomain->N        = N;
  subdomain->Np       = Nrp;
  subdomain->Nfp      = 1;
  subdomain->Nfaces   = 2;
  subdomain->Ncorners = 2;

  subdomain->r = bfam_malloc_aligned(Nrp*sizeof(bfam_real_t));
  subdomain->w = bfam_malloc_aligned(Nrp*sizeof(bfam_real_t));
  subdomain->wi = bfam_malloc_aligned(Nrp*sizeof(bfam_real_t));
  for(int n = 0; n < Nrp; ++n)
  {
    subdomain->r[n] = (bfam_real_t) lr[0][n];
    subdomain->w[n] = (bfam_real_t) lw[n];
    subdomain->wi[n] = 1.0/subdomain->w[n];
  }

  subdomain->K = K;

  subdomain->num_interp = num_interp;

  subdomain->interpolation =
    bfam_malloc_aligned(subdomain->num_interp * sizeof(bfam_real_t*));

  subdomain->massprojection =
    bfam_malloc_aligned(subdomain->num_interp * sizeof(bfam_real_t*));

  subdomain->exact_mass =
    bfam_malloc_aligned(Nrp*Nrp*sizeof(bfam_real_t));
  for(int n = 0; n < Nrp*Nrp; ++n)
    subdomain->exact_mass[n] = (bfam_real_t) mass[n];

  for(int i = 0; i < subdomain->num_interp; ++i)
  {
    if(i == 0 && N_m == N)
    {
      /*
       * Identity interpolation and projection operator
       */
      subdomain->interpolation[i] = NULL;
    }
    else
    {
      subdomain->interpolation[i] =
        bfam_malloc_aligned(Nrp * sub_m_Nrp * sizeof(bfam_real_t));
      for(int n = 0; n < Nrp*sub_m_Nrp; ++n)
        subdomain->interpolation[i][n] = (bfam_real_t) interpolation[i][n];
    }

    subdomain->massprojection[i] =
        bfam_malloc_aligned(Nrp * sub_m_Nrp * sizeof(bfam_real_t));

    for(int n = 0; n < Nrp*sub_m_Nrp; ++n)
      subdomain->massprojection[i][n] = (bfam_real_t) massprojection[i][n];
  }

  subdomain->EToEp = bfam_malloc_aligned(K*sizeof(bfam_locidx_t));
  subdomain->EToEm = bfam_malloc_aligned(K*sizeof(bfam_locidx_t));
  subdomain->EToFm = bfam_malloc_aligned(K*sizeof(int8_t));
  subdomain->EToHm = bfam_malloc_aligned(K*sizeof(int8_t));
  subdomain->EToOm = bfam_malloc_aligned(K*sizeof(int8_t));

  qsort(mapping, K, sizeof(bfam_subdomain_face_map_entry_t),
      bfam_subdomain_face_send_cmp);

  for(bfam_locidx_t k = 0; k < K; ++k)
    mapping[k].i = k;

  qsort(mapping, K, sizeof(bfam_subdomain_face_map_entry_t),
      bfam_subdomain_face_recv_cmp);

  for(bfam_locidx_t k = 0; k < K; ++k)
  {
    subdomain->EToEm[k] = ktok_m[mapping[k].k];
    subdomain->EToFm[k] = mapping[k].f;
    subdomain->EToHm[k] = mapping[k].h;
    subdomain->EToOm[k] = mapping[k].o;

    subdomain->EToEp[k] = mapping[k].i;
  }

#ifdef BFAM_DEBUG
  for(bfam_locidx_t k = 0; k < K; ++k)
    BFAM_ASSERT(mapping[k].s  == subdomain->s_m &&
                mapping[k].ns == subdomain->s_p);
#endif

  for(int i = 0; i < num_interp; ++i)
  {
    bfam_free_aligned(lr[i]);
    bfam_free_aligned(interpolation[i]);
    bfam_free_aligned(massprojection[i]);
  }

  bfam_free_aligned(V);
  bfam_free_aligned(mass);

  bfam_free_aligned(lr);
  bfam_free_aligned(lw);

  bfam_free_aligned(interpolation);
  bfam_free_aligned(massprojection);

  bfam_free_aligned(sub_m_lr);
  bfam_free_aligned(sub_m_lw);
  bfam_free_aligned(sub_m_V);
}

void
bfam_subdomain_dgx_quad_glue_free(bfam_subdomain_t *subdomain)
{
  bfam_subdomain_dgx_quad_glue_t *sub =
    (bfam_subdomain_dgx_quad_glue_t*) subdomain;

  bfam_dictionary_allprefixed_ptr(&sub->base.fields,"",
      &bfam_subdomain_dgx_quad_free_fields,NULL);
  bfam_dictionary_allprefixed_ptr(&sub->base.fields_p,"",
      &bfam_subdomain_dgx_quad_free_fields,NULL);
  bfam_dictionary_allprefixed_ptr(&sub->base.fields_m,"",
      &bfam_subdomain_dgx_quad_free_fields,NULL);
  bfam_dictionary_allprefixed_ptr(&sub->base.fields_face,"",
      &bfam_subdomain_dgx_quad_free_fields,NULL);

  bfam_free_aligned(sub->r);
  bfam_free_aligned(sub->w);
  bfam_free_aligned(sub->wi);

  for(int i = 0; i < sub->num_interp; ++i)
    if(sub->interpolation[i])
      bfam_free_aligned(sub->interpolation[i]);
  bfam_free_aligned(sub->interpolation);

  for(int i = 0; i < sub->num_interp; ++i)
    if(sub->massprojection[i])
      bfam_free_aligned(sub->massprojection[i]);
  bfam_free_aligned(sub->massprojection);

  bfam_free_aligned(sub->EToEp);
  bfam_free_aligned(sub->EToEm);
  bfam_free_aligned(sub->EToFm);
  bfam_free_aligned(sub->EToHm);
  bfam_free_aligned(sub->EToOm);

  bfam_subdomain_free(subdomain);
}
