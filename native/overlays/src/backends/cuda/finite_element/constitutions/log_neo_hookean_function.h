// Local extension to libuipc (Apache-2.0): non-inverting compressible NH.
// Physical Lame parameters, column-major vectorization of F.
#pragma once
#include <type_define.h>
#include <Eigen/Dense>
#include <cmath>
#include <limits>

namespace uipc::backend::cuda::log_nh
{
constexpr Float min_jacobian = 1e-8;

UIPC_GENERIC Float triple(const Vector3& a, const Vector3& b, const Vector3& c)
{
    return a.dot(b.cross(c));
}

UIPC_GENERIC Matrix3x3 cofactor(const Matrix3x3& F)
{
    Matrix3x3 C;
    for(int i=0; i<3; ++i)
        for(int j=0; j<3; ++j)
            C(i,j)=F((i+1)%3,(j+1)%3)*F((i+2)%3,(j+2)%3)
                   -F((i+1)%3,(j+2)%3)*F((i+2)%3,(j+1)%3);
    return C;
}

UIPC_GENERIC Float determinant(const Matrix3x3& F)
{
    return F(0,0)*(F(1,1)*F(2,2)-F(1,2)*F(2,1))
          -F(0,1)*(F(1,0)*F(2,2)-F(1,2)*F(2,0))
          +F(0,2)*(F(1,0)*F(2,1)-F(1,1)*F(2,0));
}

// Check the entire Newton trial segment. Positive endpoints alone can skip
// two roots of det(F0 + s*dF). Its minimum is at an endpoint or a derivative root.
UIPC_GENERIC bool feasible_segment(const Matrix3x3& A, const Matrix3x3& end)
{
    const Matrix3x3 B = end - A;
    const Float c0 = determinant(A);
    const Float c1 = triple(B.col(0), A.col(1), A.col(2))
                   + triple(A.col(0), B.col(1), A.col(2))
                   + triple(A.col(0), A.col(1), B.col(2));
    const Float c2 = triple(A.col(0), B.col(1), B.col(2))
                   + triple(B.col(0), A.col(1), B.col(2))
                   + triple(B.col(0), B.col(1), A.col(2));
    const Float c3 = determinant(B);
    if(!(c0 > min_jacobian && determinant(end) > min_jacobian))
        return false;
    const Float a = 3*c3, b = 2*c2, c = c1;
    Float roots[2] = {-1, -1};
    const Float scale = fabs(a) + fabs(b) + fabs(c);
    const Float eps = 32*2.2204460492503131e-16*scale;
    if(fabs(a) <= eps)
    {
        if(fabs(b) > eps) roots[0] = -c/b;
    }
    else
    {
        const Float disc = b*b - 4*a*c;
        if(disc >= 0)
        {
            const Float q = -0.5*(b + copysign(sqrt(disc), b));
            if(q == 0) roots[0] = -b/(2*a);
            else { roots[0] = q/a; roots[1] = c/q; }
        }
    }
    for(int i=0; i<2; ++i)
    {
        const Float s = roots[i];
        if(s > 0 && s < 1 && !(((c3*s+c2)*s+c1)*s+c0 > min_jacobian))
            return false;
    }
    return true;
}

UIPC_GENERIC void E(Float& energy, Float mu, Float lambda, const Matrix3x3& F)
{
    const Float J = determinant(F);
    if(!(J > min_jacobian)) { energy = HUGE_VAL; return; }
    const Float logJ = log(J);
    energy = mu/2*(F.squaredNorm()-3) - mu*logJ + lambda/2*logJ*logJ;
}

UIPC_GENERIC void dEdF(Matrix3x3& P, Float mu, Float lambda, const Matrix3x3& F)
{
    const Float J=determinant(F);
    const Matrix3x3 C=cofactor(F);
    const Float q=(lambda*log(J)-mu)/J;
    for(int i=0; i<9; ++i) P.data()[i]=mu*F.data()[i]+q*C.data()[i];
}

UIPC_GENERIC void raw_hessian(Matrix9x9& H, Float mu, Float lambda, const Matrix3x3& F)
{
    const Float J=determinant(F);
    const Matrix3x3 G=cofactor(F)/J;
    const Float q = lambda*log(J)-mu;
    for(int a=0; a<3; ++a)
        for(int i=0; i<3; ++i)
            for(int b=0; b<3; ++b)
                for(int j=0; j<3; ++j)
                    H(i+3*a,j+3*b) = mu*(i==j && a==b)
                        + lambda*G(i,a)*G(j,b) - q*G(i,b)*G(j,a);
}

UIPC_GENERIC void ddEddF_spd(Matrix9x9& H, Float mu, Float lambda, const Matrix3x3& F)
{
    raw_hessian(H, mu, lambda, F);
    // Bounded cyclic Jacobi EVD with explicit small-matrix arithmetic for CUDA.
    Matrix9x9 Q=Matrix9x9::Identity();
    const Float threshold=1e-13*H.norm();
    for(int sweep=0; sweep<32; ++sweep)
    {
        Float maximum=0;
        for(int p=0; p<8; ++p)
            for(int q=p+1; q<9; ++q)
            {
                const Float apq=H(p,q);
                maximum=fmax(maximum,fabs(apq));
                if(fabs(apq)<=threshold) continue;
                const Float tau=(H(q,q)-H(p,p))/(2*apq);
                const Float t=copysign(1/(fabs(tau)+sqrt(1+tau*tau)),tau);
                const Float c=1/sqrt(1+t*t), s=t*c;
                H(p,p)-=t*apq; H(q,q)+=t*apq;
                H(p,q)=H(q,p)=0;
                for(int k=0; k<9; ++k)
                {
                    if(k!=p && k!=q)
                    {
                        const Float kp=H(k,p), kq=H(k,q);
                        H(k,p)=H(p,k)=c*kp-s*kq;
                        H(k,q)=H(q,k)=s*kp+c*kq;
                    }
                    const Float qp=Q(k,p), qq=Q(k,q);
                    Q(k,p)=c*qp-s*qq; Q(k,q)=s*qp+c*qq;
                }
            }
        if(maximum<=threshold) break;
    }
    Vector<Float,9> values;
    for(int i=0; i<9; ++i) values(i)=fmax(H(i,i),Float{0});
    // Explicit small product avoids Eigen's host-oriented blocked GEMM path.
    for(int i=0; i<9; ++i)
        for(int j=0; j<9; ++j)
        {
            Float value=0;
            for(int k=0; k<9; ++k) value+=Q(i,k)*values(k)*Q(j,k);
            H(i,j)=value;
        }
}
} // namespace uipc::backend::cuda::log_nh
