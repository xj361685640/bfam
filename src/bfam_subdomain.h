#ifndef BFAM_SUBDOMAIN_H
#define BFAM_SUBDOMAIN_H

#include <bfam_base.h>
#include <bfam_critbit.h>
#include <bfam_dictionary.h>

typedef struct bfam_subdomain_face_map_entry
{
  bfam_locidx_t np; /* Neighbor's processor number */
  bfam_locidx_t ns; /* Neighbor's subdomain id */
  bfam_locidx_t nk; /* Neighbor's element number */
  int8_t        nf; /* Neighbor's face number */
  int8_t        nh; /* Neighbor's hanging number */

  bfam_locidx_t  s; /* Local subdomain id */
  bfam_locidx_t  k; /* Local element number */
  int8_t         f; /* Local face number */
  int8_t         h; /* Local hanging number */
  int8_t         o; /* Local orientation */

  bfam_locidx_t gi; /* Index variable */
  bfam_locidx_t  i; /* Index variable */
} bfam_subdomain_face_map_entry_t;

/*
 * Compare function which sorts a bfam_subdomain_face_map_entry_t array
 * in sending order.
 */
int
bfam_subdomain_face_send_cmp(const void *a, const void *b);

/*
 * Compare function which sorts a bfam_subdomain_face_map_entry_t array
 * in receiving order.
 */
int
bfam_subdomain_face_recv_cmp(const void *a, const void *b);

struct bfam_subdomain;

/*
 * This is the field initialization function which will be called for each
 * subdomain.  This function fills the \a npoints values of \a field.
 *
 * \param [in]  npoints   this is the length of x, y, z, and field
 * \param [in]  x         x-coordinates of the points
 * \param [in]  y         y-coordinates of the points
 * \param [in]  z         z-coordinates of the points
 * \param [in]  s         pointer to the subdomain that the field is in
 * \param [in]  arg       user pointer
 * \param [out] field     the field values that need to be set
 *
 */
typedef void (*bfam_subdomain_init_field_t) (bfam_locidx_t npoints,
    bfam_real_t time, bfam_real_t *restrict x, bfam_real_t *restrict y,
    bfam_real_t *restrict z, struct bfam_subdomain *s, void *arg,
    bfam_real_t *restrict field);


/**
 * base structure for all subdomains types. Any new subdomain should have this
 * as its first member with the name base, i.e.,
 * \code{.c}
 * typedef struct new_subdomain_type
 * {
 *   bfam_subdomain_t base;
 *   ...
 * }
 */
typedef struct bfam_subdomain
{
  bfam_locidx_t   id;
  char*           name;     /**< Name of the subdomain */
  bfam_critbit0_tree_t tags; /**< critbit for tags for the subdomain */
  bfam_dictionary_t fields; /**< a dictionary storing pointers to fields */
  bfam_dictionary_t fields_m; /**< a dictionary storing minus fields */
  bfam_dictionary_t fields_p; /**< a dictionary storing plus fields */
  bfam_dictionary_t fields_face; /**< a dictionary storing face fields */

  /* Function pointers that domain will need to call */
  void (*free)                (struct bfam_subdomain *thisSubdomain);

  /**< Write a vtk file */
  void (*vtk_write_vtu_piece) (struct bfam_subdomain *thisSubdomain,
                               FILE *file,
                               const char **scalars,
                               const char **vectors,
                               const char **components,
                               int writeBinary,
                               int writeCompressed,
                               int rank,
                               bfam_locidx_t id);

  /**< Add a field to the subdomain */
  int (*field_add) (struct bfam_subdomain *thisSubdomain, const char* name);

  /**< Add a field to the plus side of the subdomain */
  int (*field_plus_add) (struct bfam_subdomain *thisSubdomain,
                         const char* name);
  /**< Add a field to the minus side of the  subdomain */
  int (*field_minus_add) (struct bfam_subdomain *thisSubdomain,
                          const char* name);

  /**< Add a field to the faces of the subdomain */
  int (*field_face_add) (struct bfam_subdomain *thisSubdomain,
                         const char* name);


  /**< Initialize a field in the subdomain */
  void (*field_init) (struct bfam_subdomain *thisSubdomain, const char* name,
      bfam_real_t time, bfam_subdomain_init_field_t init_field, void *arg);

  /**< Glue grid communication info */
  void (*glue_comm_info) (struct bfam_subdomain *thisSubdomain, int *rank,
      bfam_locidx_t *my_id, bfam_locidx_t *neigh_id,
      size_t *send_sz, size_t *recv_sz);

  /**< Put data into the send buffer */
  void (*glue_put_send_buffer) (struct bfam_subdomain *thisSubdomain,
      void *buffer, size_t send_sz);

  /**< Get data from the recv buffer */
  void (*glue_get_recv_buffer) (struct bfam_subdomain *thisSubdomain,
      void *buffer, size_t recv_sz);
} bfam_subdomain_t;


/** initializes a subdomain
 *
 * There no new function for subdomains since these are really just a base class
 * and a concrete grid and physics type should be defined
 *
 * \param [in,out] thisSubdomain pointer to the subdomain
 * \param [in]     id   Unique id number for this subdomain
 * \param [in]     name Name of this subdomain
 */
void
bfam_subdomain_init(bfam_subdomain_t *subdomain, bfam_locidx_t id,
    const char* name);

/** free up the memory allocated by the subdomain
 * 
 * \param [in,out] thisSubdomain subdomain to clean up
 */
void
bfam_subdomain_free(bfam_subdomain_t *thisSubdomain);

/** Add a tag to the subdomain
 *
 * \param [in,out] thisSubdomain subdomain to andd the tag to
 * \param [in]     tag           tag of the domain (\0 terminated string)
 *
 */
void
bfam_subdomain_add_tag(bfam_subdomain_t *thisSubdomain, const char* tag);

/** Remove a tag from the subdomain
 *
 * \param [in,out] thisSubdomain subdomain to remove the tag from
 * \param [in]     tag           tag of the domain (\0 terminated string)
 *
 * \returns It returns 1 if the tag was removed, 0 otherwise.
 */
int
bfam_subdomain_delete_tag(bfam_subdomain_t *thisSubdomain, const char* tag);

/** Check to see if a subdomain has a tag
 *
 * \param [in,out] thisSubdomain subdomain to search for the tag
 * \param [in]     tag           tag of the domain (\0 terminated string)
 *
 * \return nonzero iff \a thisSubdomain has the tag \a tag
 */
int
bfam_subdomain_has_tag(bfam_subdomain_t *thisSubdomain, const char* tag);

/** Add a field to the subdomain
 *
 * \param [in,out] thisSubdomain subdomain to search for the tag
 * \param [in]     name          name of the field to add to the subdomain
 *
 * \returns:
 *   $\cases{ 0 &if {\rm out of memory} \cr
 *            1 &if {\it name} {\rm was already a field} \cr
 *            2 &if {\it name} {\rm was added successfully}}$.
 */
int
bfam_subdomain_field_add(bfam_subdomain_t *thisSubdomain, const char* name);

/** Add a field to the plus side of the subdomain.
 *
 * \param [in,out] thisSubdomain subdomain to search for the tag
 * \param [in]     name          name of the field to add to the subdomain
 *
 * \returns:
 *   $\cases{ 0 &if {\rm out of memory} \cr
 *            1 &if {\it name} {\rm was already a field} \cr
 *            2 &if {\it name} {\rm was added successfully}}$.
 */
int
bfam_subdomain_field_plus_add(bfam_subdomain_t *thisSubdomain,
                              const char* name);

/** Add a field to the minus side of the subdomain.
 *
 * \param [in,out] thisSubdomain subdomain to search for the tag
 * \param [in]     name          name of the field to add to the subdomain
 *
 * \returns:
 *   $\cases{ 0 &if {\rm out of memory} \cr
 *            1 &if {\it name} {\rm was already a field} \cr
 *            2 &if {\it name} {\rm was added successfully}}$.
 */
int
bfam_subdomain_field_minus_add(bfam_subdomain_t *thisSubdomain,
                               const char* name);

/** Add a field to the faces of the subdomain.
 *
 * \param [in,out] thisSubdomain subdomain to search for the tag
 * \param [in]     name          name of the field to add to the subdomain
 *
 * \returns:
 *   $\cases{ 0 &if {\rm out of memory} \cr
 *            1 &if {\it name} {\rm was already a field} \cr
 *            2 &if {\it name} {\rm was added successfully}}$.
 */
int
bfam_subdomain_field_face_add(bfam_subdomain_t *thisSubdomain,
                              const char* name);

/** Initialize a field in the subdomain
 *
 * \param [in,out] thisSubdomain subdomain to search for the tag
 * \param [in]     name          name of the field to initialize
 * \param [in]     time          time to pass to initilization function
 * \param [in]     init_field     initilization function
 * \param [in]     arg           user pointer
 */
void
bfam_subdomain_field_init(bfam_subdomain_t *thisSubdomain, const char* name,
      bfam_real_t time, bfam_subdomain_init_field_t init_field, void *arg);

#endif
