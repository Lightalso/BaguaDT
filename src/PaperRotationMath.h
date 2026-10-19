#pragma once
#include <cmath>
#ifdef __CUDACC__
#define BAGUA_HD __host__ __device__
#else
#define BAGUA_HD
#endif
namespace bagua {
// Paper Eq. (10): a = F_A a_rest. Deliberately no normalization of the affine axis.
BAGUA_HD inline void paperRodrigues(const double *fa,const double *axisRest,double theta,double *rotation) {
    double a[3]={0,0,0};for(int i=0;i<3;++i)for(int j=0;j<3;++j)a[i]+=fa[i*3+j]*axisRest[j];
    double k[9]={0,-a[2],a[1],a[2],0,-a[0],-a[1],a[0],0};
    double sine=::sin(theta),cosine=1-::cos(theta);
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){double square=0;for(int q=0;q<3;++q)square+=k[i*3+q]*k[q*3+j];rotation[i*3+j]=(i==j?1:0)+sine*k[i*3+j]+cosine*square;}
}
// Relative-frame form of the lagged quadratic rotation constraint. R is frozen for
// the entire Newton linearization AND all backtracking trials of that iteration.
// q=[translation, row-major F] for each 12-DOF ABD; alignment=F_A0^-1 F_B0.
BAGUA_HD inline double paperQuadraticRotation(const double *qa,const double *qb,const double *rotation,
        const double *alignment,double stiffness,double *gradient=nullptr,double *hessian=nullptr) {
    double residual[9]={0},jacobian[9*24]={0};double energy=0;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
        int row=i*3+j;double value=-qb[3+row];
        for(int p=0;p<3;++p)for(int q=0;q<3;++q){double coefficient=rotation[i*3+p]*alignment[q*3+j];value+=coefficient*qa[3+p*3+q];jacobian[row*24+3+p*3+q]=coefficient;}
        residual[row]=value;jacobian[row*24+15+row]=-1;energy+=.5*stiffness*value*value;
    }
    if(gradient)for(int i=0;i<24;++i){double sum=0;for(int r=0;r<9;++r)sum+=jacobian[r*24+i]*residual[r];gradient[i]=stiffness*sum;}
    if(hessian)for(int i=0;i<24;++i)for(int j=0;j<24;++j){double sum=0;for(int r=0;r<9;++r)sum+=jacobian[r*24+i]*jacobian[r*24+j];hessian[i*24+j]=stiffness*sum;}
    return energy;
}
}
#undef BAGUA_HD
