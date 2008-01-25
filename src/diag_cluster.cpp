/* -----------------------q-----------------------------------------------
   SPPARKS - Stochastic Parallel PARticle Kinetic Simulator
   contact info, copyright info, etc
------------------------------------------------------------------------- */

#include "mpi.h"
#include "math.h"
#include "stdlib.h"
#include "string.h"
#include "output.h"
#include "memory.h"
#include "app.h"
#include "error.h"
#include "timer.h"
#include "diag_cluster.h"
#include "app_lattice.h"
#include "comm_lattice.h"
#include "random_park.h"

using namespace SPPARKS;

/* ---------------------------------------------------------------------- */

DiagCluster::DiagCluster(SPK *spk, int narg, char **arg) : Diag(spk,narg,arg)
{
  cluster_ids = NULL;
  comm = NULL;
  fp = NULL;
  fpdump = NULL;
  clustlist = NULL;
  ncluster = 0;
  idump = 0;
  dump_style = STANDARD;

  int iarg = 2;
  while (iarg < narg) {
    if (strcmp(arg[iarg],"filename") == 0) {
      iarg++;
      if (iarg < narg) {
	if (me == 0) {
	  fp = fopen(arg[iarg],"w");
	  if (!fp) error->one("Cannot open diag_style cluster output file");
	}
      } else {
	error->all("Illegal diag_style cluster command");
      } 
    } else if (strcmp(arg[iarg],"dump_style") == 0) {
      iarg++;
      if (iarg < narg) {
	if (strcmp(arg[iarg],"standard") == 0) {
	  idump = 1;
	  dump_style = STANDARD;
	  iarg++;
	  if (iarg < narg) {
	    if (me == 0) {
	      fpdump = fopen(arg[iarg],"w");
	      if (!fpdump) error->one("Cannot open diag_style cluster dump file");
	    }
	  } else {
	    error->all("Illegal diag_style cluster command");
	  }
	  // OpenDX not yet supported
// 	} else if (strcmp(arg[iarg],"opendx") == 0) {
// 	  idump = 1;
// 	  dump_style = OPENDX;
// 	  iarg++;
// 	  if (iarg < narg) {
// 	    int n = strlen(arg[iarg]) + 1;
// 	    opendxroot = new char[n];
// 	    strcpy(opendxroot,arg[iarg]);
// 	    opendxcount = 0;
// 	  } else {
// 	    error->all("Illegal diag_style cluster command");
// 	  }
	} else if (strcmp(arg[iarg],"none") == 0) {
	  idump = 0;
	} else {
	    error->all("Illegal diag_style cluster command");
	}
      } else {
	error->all("Illegal diag_style cluster command");
      }
    } else {
      //      error->all("Illegal diag_style cluster command");
    }
    iarg++;
  }
}

/* ---------------------------------------------------------------------- */

DiagCluster::~DiagCluster()
{
  delete comm;
  memory->destroy_1d_T_array(cluster_ids,0);
  free_clustlist();
  if (me == 0 ) {
    if (fp) fclose(fp);
    if (fpdump) fclose(fpdump);
  }
}

/* ---------------------------------------------------------------------- */

void DiagCluster::init(double time)
{

  if (app->appclass != App::LATTICE) error->all("diag_style incompatible with app_style");

  applattice = (AppLattice *) app;
  nglobal = applattice->nglobal;
  nlocal = applattice->nlocal;
  nghost = applattice->nghost;
  boxxlo = applattice->boxxlo;
  boxxhi = applattice->boxxhi;
  boxylo = applattice->boxylo;
  boxyhi = applattice->boxyhi;
  boxzlo = applattice->boxzlo;
  boxzhi = applattice->boxzhi;
  xyz = applattice->xyz;
  id = applattice->id;

  memory->destroy_1d_T_array(cluster_ids,0);
  memory->create_1d_T_array(cluster_ids,0,nlocal+nghost-1,"diagcluster:cluster");

  if (!comm) comm = new CommLattice(spk);
  comm->init(NULL,applattice->delghost,applattice->dellocal,cluster_ids);

  write_header();
  analyze_clusters(time);

  setup_time(time);
}


/* ---------------------------------------------------------------------- */

void DiagCluster::compute(double time, int done)
{
  if (check_time(time, done)) analyze_clusters(time);
}

/* ---------------------------------------------------------------------- */

void DiagCluster::analyze_clusters(double time)
{
  if (me == 0) {
    if (fp) {
      fprintf(fp,"\n\n--------------------------------------------------\n");
      fprintf(fp,"Time = %f \n",time);
    }
  }
  free_clustlist();
  generate_clusters();
   if (idump) {
     dump_clusters(time);
   }
}
/* ---------------------------------------------------------------------- */

void DiagCluster::write_header()
{
  if (me == 0) {
    if (fp) {
      fprintf(fp,"Clustering Analysis for Lattice (diag_style cluster) \n");
      fprintf(fp,"nglobal = %d \n",nglobal);
      fprintf(fp,"nprocs = %d \n",nprocs);
    }
  }
}

/* ----------------------------------------------------------------------
   Perform cluster analysis using a definition
   of connectivity provided by the child application
------------------------------------------------------------------------- */

void DiagCluster::generate_clusters()
{

  // Psuedocode
  //
  // work on copy of spin array since spin values are changed
  // all id values = 0 initially

  // loop n over all owned sites {
  //   if (id[n] = 0) {
  //     area = 0
  //   } else continue

  //   push(n)
  //   while (stack not empty) {
  //     i = pop()
  //     loop over m neighbor pixels of i:
  //       if (spin[m] == spin[n] and not outside domain) push(m)
  //   }

  // void push(m)
  //   id[m] = id
  //   stack[nstack++] = m

  // int pop()
  //   return stack[nstack--]
  //   area++

  // Set ghost site ids to -1
  for (int i = nlocal; i < nlocal+nghost; i++) {
    cluster_ids[i] = -1;
  }

  // Set local site ids to zero 
  for (int i = 0; i < nlocal; i++) {
    cluster_ids[i] = 0;
  }

  int vol,volsum,voltot,nclustertot,ii,jj,kk,id;

  ncluster = 0;
  volsum = 0;

  // loop over all owned sites
  for (int i = 0; i < nlocal; i++) {
    // If already visited, skip
    if (cluster_ids[i] != 0) {
      continue;
    }
    
    // Push first site onto stack
    id = ncluster+1;
    vol = 0;
    add_cluster(id,vol,0,NULL);
    cluststack.push(i);
    cluster_ids[i] = id;

    while (cluststack.size()) {
      // First top then pop
      ii = cluststack.top();
      cluststack.pop();
      vol++;
      applattice->push_connected_neighbors(ii,cluster_ids,ncluster,&cluststack);
    }
    clustlist[ncluster-1].volume = vol;
    volsum+=vol;
  }

  int idoffset;
  MPI_Allreduce(&volsum,&voltot,1,MPI_INT,MPI_SUM,world);
  MPI_Allreduce(&ncluster,&nclustertot,1,MPI_INT,MPI_SUM,world);
  MPI_Scan(&ncluster,&idoffset,1,MPI_INT,MPI_SUM,world);
  idoffset = idoffset-ncluster+1;
  for (int i = 0; i < ncluster; i++) {
    clustlist[i].global_id = i+idoffset;
  }

  // change site ids to global ids
  for (int i = 0; i < nlocal; i++) {
    cluster_ids[i] = clustlist[cluster_ids[i]-1].global_id;
  }

  // Communicate side ids
  comm->all();

  // loop over all owned sites adjacent to boundary
  for (int i = 0; i < nlocal; i++) {
    applattice->connected_ghosts(i,cluster_ids,clustlist,idoffset);
  }

  // pack my clusters into buffer
  int me_size,m,maxbuf;
  int* ibufclust;
  int tmp,nrecv;
  MPI_Status status;
  MPI_Request request;
  int nn;
  
  me_size = 0;
  for (int i = 0; i < ncluster; i++) {
    me_size += 3+clustlist[i].nneigh;
  }
  if (me == 0) me_size = 0;

  MPI_Allreduce(&me_size,&maxbuf,1,MPI_INT,MPI_MAX,world);

  ibufclust = new int[maxbuf];

  if (me != 0) {
    m = 0;
    for (int i = 0; i < ncluster; i++) {
      ibufclust[m++] = clustlist[i].global_id;
      ibufclust[m++] = clustlist[i].volume;
      ibufclust[m++] = clustlist[i].nneigh;
      for (int j = 0; j < clustlist[i].nneigh; j++) {
	ibufclust[m++] = clustlist[i].neighlist[j];
      }
    }
    
    if (me_size != m) {
      error->one("Mismatch in counting for ibufclust");
    }

  }

  // proc 0 pings each proc, receives it's data, adds it to list
  // all other procs wait for ping, send their data to proc 0

  if (me == 0) {
    for (int iproc = 1; iproc < nprocs; iproc++) {
      MPI_Irecv(ibufclust,maxbuf,MPI_INT,iproc,0,world,&request);
      MPI_Send(&tmp,0,MPI_INT,iproc,0,world);
      MPI_Wait(&request,&status);
      MPI_Get_count(&status,MPI_INT,&nrecv);
      
      m = 0;
      while (m < nrecv) {
	id = ibufclust[m++];
	vol = ibufclust[m++];
	nn = ibufclust[m++];
	add_cluster(id,vol,nn,&ibufclust[m]);
	m+=nn;
	volsum+=vol;
      }
    }
  } else {
    MPI_Recv(&tmp,0,MPI_INT,0,0,world,&status);
    MPI_Rsend(ibufclust,me_size,MPI_INT,0,0,world);
  }

  delete [] ibufclust;

  // Perform cluster analysis on the clusters

  if (me == 0) {
    int* neighs;
    int jneigh,ncluster_reduced;

    volsum = 0;
    ncluster_reduced = 0;

    // loop over all clusters
    for (int i = 0; i < ncluster; i++) {
      
      // If already visited, skip
      if (clustlist[i].volume == 0) {
	continue;
      }
      
      // Push first cluster onto stack
      id = clustlist[i].global_id;
      vol = 0;
      ncluster_reduced++;
      
      cluststack.push(i);
      vol+=clustlist[i].volume;
      clustlist[i].volume = 0;
      
      while (cluststack.size()) {
	// First top then pop
	ii = cluststack.top();
	cluststack.pop();
	
	neighs = clustlist[ii].neighlist;
	for (int j = 0; j < clustlist[ii].nneigh; j++) {
	  jneigh = neighs[j]-idoffset;
	  if (clustlist[jneigh].volume != 0) {
	    cluststack.push(jneigh);
	    vol+=clustlist[jneigh].volume;
	    clustlist[jneigh].global_id = id;
	    clustlist[jneigh].volume = 0;
	  }
	}
      }
      clustlist[i].volume = vol;
      volsum+=vol;
    }
    
    if (fp) {
      fprintf(fp,"ncluster = %d \nsize = ",ncluster_reduced);
      for (int i = 0; i < ncluster; i++) {
	if (clustlist[i].volume > 0.0) {
	  fprintf(fp," %d",clustlist[i].volume);
	}
      }
      fprintf(fp,"\n");
    }
  }
  
}


/* ---------------------------------------------------------------------- */

void DiagCluster::add_cluster(int id, int vol, int nn, int* neighs)
{
  // grow cluster array

  ncluster++;
  clustlist = (Cluster *) memory->srealloc(clustlist,ncluster*sizeof(Cluster),
					 "diagcluster:clustlist");
  clustlist[ncluster-1] = Cluster(id,vol,nn,neighs);
}

/* ----------------------------------------------------------------------
   dump a snapshot of cluster identities to file pointer fpdump
------------------------------------------------------------------------- */

void DiagCluster::dump_clusters(double time)
{
  int nsend,nrecv;
  double* dbuftmp;
  int maxbuftmp;
  int cid;

  if (me == 0) {
    if (dump_style == STANDARD) {
      fprintf(fpdump,"ITEM: TIMESTEP\n");
      fprintf(fpdump,"%g\n",time);
      fprintf(fpdump,"ITEM: NUMBER OF ATOMS\n");
      fprintf(fpdump,"%d\n",nglobal);
      fprintf(fpdump,"ITEM: BOX BOUNDS\n");
      fprintf(fpdump,"%g %g\n",boxxlo,boxxhi);
      fprintf(fpdump,"%g %g\n",boxylo,boxyhi);
      fprintf(fpdump,"%g %g\n",boxzlo,boxzhi);
      fprintf(fpdump,"ITEM: ATOMS\n");
    }
  }

  int size_one = 5;

  maxbuftmp = 0;
  MPI_Allreduce(&nlocal,&maxbuftmp,1,MPI_INT,MPI_MAX,world);
  dbuftmp = (double*) memory->smalloc(size_one*maxbuftmp*sizeof(double),"diagcluster:dump_clusters:buftmp");

  int m = 0;

  // pack my lattice values into buffer

  for (int i = 0; i < nlocal; i++) {
    dbuftmp[m++] = id[i];
    dbuftmp[m++] = cluster_ids[i];
    dbuftmp[m++] = xyz[i][0];
    dbuftmp[m++] = xyz[i][1];
    dbuftmp[m++] = xyz[i][2];
  }

  // proc 0 pings each proc, receives it's data, writes to file
  // all other procs wait for ping, send their data to proc 0

  int tmp;
  MPI_Status status;
  MPI_Request request;

  if (me == 0) {
    for (int iproc = 0; iproc < nprocs; iproc++) {
      if (iproc) {
	MPI_Irecv(dbuftmp,size_one*maxbuftmp,MPI_DOUBLE,iproc,0,world,&request);
	MPI_Send(&tmp,0,MPI_INT,iproc,0,world);
	MPI_Wait(&request,&status);
	MPI_Get_count(&status,MPI_DOUBLE,&nrecv);
	nrecv /= size_one;
      } else nrecv = nlocal;
      
      m = 0;

  // print lattice values

      if (dump_style == STANDARD) {
	for (int i = 0; i < nrecv; i++) {
	  cid = static_cast<int>(dbuftmp[m+1])-1;
	  cid = clustlist[cid].global_id;
	  fprintf(fpdump,"%d %d %g %g %g\n",
		  static_cast<int>(dbuftmp[m]),cid,
		  dbuftmp[m+2],dbuftmp[m+3],dbuftmp[m+4]);
	  m += size_one;
	}
      }
    }
    
  } else {
    MPI_Recv(&tmp,0,MPI_INT,0,0,world,&status);
    MPI_Rsend(dbuftmp,size_one*nlocal,MPI_DOUBLE,0,0,world);
  }

  memory->sfree(dbuftmp);

}

/* ---------------------------------------------------------------------- */

void DiagCluster::free_clustlist()
{
  // Can not call Cluster destructor, because 
  // that would free memory twice.
  // Instead, need to delete neighlist manually.
  for (int i = 0; i < ncluster; i++) {
    free(clustlist[i].neighlist);
  }
  memory->sfree(clustlist);
  clustlist = NULL;
  ncluster = 0;
}

/* ---------------------------------------------------------------------- */

void DiagCluster::stats(char *strtmp) {
  sprintf(strtmp," %10d",ncluster);
}

/* ---------------------------------------------------------------------- */

void DiagCluster::stats_header(char *strtmp) {
  sprintf(strtmp," %10s","Nclust");
}