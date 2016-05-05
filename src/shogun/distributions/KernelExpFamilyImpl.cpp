/*
 * Copyright (c) The Shogun Machine Learning Toolbox
 * Written (w) 2016 Heiko Strathmann
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are those
 * of the authors and should not be interpreted as representing official policies,
 * either expressed or implied, of the Shogun Development Team.
 */

#include <shogun/distributions/KernelExpFamilyImpl.h>
#include <shogun/lib/config.h>
#include <shogun/lib/SGMatrix.h>
#include <shogun/lib/SGVector.h>
#include <shogun/mathematics/eigen3.h>
#include <shogun/mathematics/Math.h>

using namespace shogun;
using namespace Eigen;

index_t KernelExpFamilyImpl::get_dimension()
{
	return m_data.num_rows;
}

index_t KernelExpFamilyImpl::get_num_data()
{
	return m_data.num_cols;
}

KernelExpFamilyImpl::KernelExpFamilyImpl(SGMatrix<float64_t> data, float64_t sigma, float64_t lambda)
{
	m_data = data;
	m_sigma = sigma;
	m_lambda = lambda;
}

float64_t KernelExpFamilyImpl::kernel(int32_t idx_a, int32_t idx_b)
{
	auto D = get_dimension();

	Map<VectorXd> x(m_data.get_column_vector(idx_a), D);
	Map<VectorXd> y(m_data.get_column_vector(idx_b), D);
	return CMath::exp(-(x-y).squaredNorm() / m_sigma);
}

SGMatrix<float64_t> KernelExpFamilyImpl::kernel_dx_dx_dy(int32_t idx_a, int32_t idx_b)
{
	auto D = get_dimension();
	Map<VectorXd> x(m_data.get_column_vector(idx_a), D);
	Map<VectorXd> y(m_data.get_column_vector(idx_b), D);
	auto diff=x-y;
	auto diff2 = diff.array().pow(2).matrix();
	auto k=CMath::exp(-diff2.sum() / m_sigma);

	SGMatrix<float64_t> result(D, D);
	Map<MatrixXd> eigen_result(result.matrix, D, D);
	eigen_result = pow(2./m_sigma,3) * k * (diff2*diff.transpose());
	eigen_result -= pow(2./m_sigma,2) * k * 2* diff.asDiagonal();
	eigen_result.rowwise() -=  (pow(2./m_sigma,2) * k * diff).transpose();

	return result;
}

SGMatrix<float64_t> KernelExpFamilyImpl::kernel_dx_dx_dy_dy(int32_t idx_a, int32_t idx_b)
{
	auto D = get_dimension();
	Map<VectorXd> x(m_data.get_column_vector(idx_a), D);
	Map<VectorXd> y(m_data.get_column_vector(idx_b), D);
	VectorXd diff2 = (x-y).array().pow(2).matrix();

	//k = gaussian_kernel(x_2d, y_2d, sigma)
	auto k=CMath::exp(-diff2.sum() / m_sigma);

	auto factor = k*pow(2.0/m_sigma, 3);
	SGMatrix<float64_t> result(D, D);
	Map<MatrixXd> eigen_result(result.matrix, D, D);

	//term1 = k * np.outer((x - y), (x - y)) ** 2 * (2.0/sigma)**4
	eigen_result = k*pow(2.0/m_sigma, 4) * (diff2*diff2.transpose());

	//term2 = k * 6 * np.diag((x - y) ** 2) * (2.0/sigma)**3  # diagonal (x-y)
	eigen_result.diagonal() -= 6*factor*diff2;

	//term3 = (1 - np.eye(d)) * k * np.tile((x - y), [d, 1]).T ** 2 * (2.0/sigma)**3  # (x_i-y_i)^2 off-diagonal
	diff2 *= factor;
	eigen_result.rowwise() -=  diff2.transpose();
	eigen_result.colwise() -=  diff2;
	eigen_result.diagonal() += 2*diff2;

	// term5 = k * (1 + 2 * np.eye(d)) * (2.0/sigma)**2
	factor = k*pow(2.0/m_sigma, 2);
	eigen_result.diagonal().array() += 2*factor;
	eigen_result.array() += factor;

	// return term1 - term2 - term3 - term3.T + term5
	return result;
}

SGMatrix<float64_t> KernelExpFamilyImpl::kernel_dx_dx(int32_t idx_a, int32_t idx_b)
{
	auto D = get_dimension();
	Map<VectorXd> x(m_data.get_column_vector(idx_a), D);
	Map<VectorXd> y(m_data.get_column_vector(idx_b), D);
	auto diff = x-y;

	//k = gaussian_kernel(x_2d, y_2d, sigma)
	auto k=CMath::exp(-diff.array().pow(2).sum() / m_sigma);

	SGMatrix<float64_t> result(D, D);
	Map<MatrixXd> eigen_result(result.matrix, D, D);

	// H = k*(2*np.eye(d)/sigma - 4*np.outer(differences, differences)/sigma**2)
	eigen_result = -diff*diff.transpose() / pow(m_sigma, 2) * k * 4;
	eigen_result.diagonal().array() += 2*k/m_sigma;

	return result;
}

SGMatrix<float64_t> KernelExpFamilyImpl::kernel_dx_dx_all()
{
	auto D = get_dimension();
	auto N = get_num_data();
	auto ND = N*D;
	SGMatrix<float64_t> result(ND,ND);
	Map<MatrixXd> eigen_result(result.matrix, ND,ND);

#pragma omp for
	for (auto idx_a=0; idx_a<N; idx_a++)
		for (auto idx_b=0; idx_b<N; idx_b++)
		{
			auto r_start = idx_a*D;
			auto c_start = idx_b*D;
			SGMatrix<float64_t> h=kernel_dx_dx(idx_a, idx_b);
			eigen_result.block(r_start, c_start, D, D) = Map<MatrixXd>(h.matrix, D, D);
			eigen_result.block(c_start, r_start, D, D) = eigen_result.block(r_start, c_start, D, D);
		}

	return result;
}

SGVector<float64_t> KernelExpFamilyImpl::compute_h()
{
	auto D = get_dimension();
	auto N = get_num_data();
	auto ND = N*D;
	SGVector<float64_t> h(ND);
	Map<VectorXd> eigen_h(h.vector, ND);

	// convenience to access column vectors
	SGMatrix<float64_t> h_mat(h.vector, D, N, false);

#pragma omp for
	for (auto idx_b=0; idx_b<N; idx_b++)
		for (auto idx_a=0; idx_a<N; idx_a++)
		{
			Map<VectorXd> result_col(h_mat.get_column_vector(idx_b), D);
			SGMatrix<float64_t> temp = kernel_dx_dx_dy(idx_a, idx_b);
			result_col += Map<MatrixXd>(temp.matrix, D,D).colwise().sum();
		}

	eigen_h /= N;

	return h;
}

float64_t KernelExpFamilyImpl::compute_xi_norm_2()
{
	auto N = get_num_data();
	auto D = get_dimension();
	float64_t xi_norm_2=0;

#pragma omp for
	for (auto idx_a=0; idx_a<N; idx_a++)
		for (auto idx_b=0; idx_b<N; idx_b++)
		{
			SGMatrix<float64_t> temp=kernel_dx_dx_dy_dy(idx_a, idx_b);
			xi_norm_2 += Map<MatrixXd>(temp.matrix, D, D).sum();
		}

	xi_norm_2 /= (N*N);

	return xi_norm_2;
}

std::pair<SGMatrix<float64_t>, SGVector<float64_t>> KernelExpFamilyImpl::build_system()
{
	auto D = get_dimension();
	auto N = get_num_data();
	auto ND = N*D;
	SGMatrix<float64_t> A(ND+1,ND+1);
	Map<MatrixXd> eigen_A(A.matrix, ND+1,ND+1);
	SGVector<float64_t> b(ND+1);
	Map<VectorXd> eigen_b(b.vector, ND+1);

	auto h = compute_h();
	auto eigen_h=Map<VectorXd>(h.vector, ND);
	auto all_hessians = kernel_dx_dx_all();
	auto eigen_all_hessians = Map<MatrixXd>(all_hessians.matrix, ND, ND);
	auto xi_norm_2 = compute_xi_norm_2();

	// A[0, 0] = np.dot(h, h) / n + lmbda * xi_norm_2
	A(0,0) = eigen_h.squaredNorm() / N + m_lambda * xi_norm_2;

	// A[1:, 1:] = np.dot(all_hessians, all_hessians) / N + lmbda * all_hessians
	eigen_A.block(1,1,ND,ND)=eigen_all_hessians*eigen_all_hessians / N + m_lambda*eigen_all_hessians;

	// A[0, 1:] = np.dot(h, all_hessians) / n + lmbda * h; A[1:, 0] = A[0, 1:]
	eigen_A.row(0).segment(1, ND) = eigen_all_hessians*eigen_h / N + m_lambda*eigen_h;
	eigen_A.col(0).segment(1, ND) = eigen_A.row(0).segment(1, ND);

	// b[0] = -xi_norm_2; b[1:] = -h.reshape(-1)
	b[0] = -xi_norm_2;
	eigen_b.segment(1, ND) = -eigen_h;

	return std::pair<SGMatrix<float64_t>, SGVector<float64_t>>(A, b);
}
