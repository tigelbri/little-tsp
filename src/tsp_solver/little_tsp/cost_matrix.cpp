#include "cost_matrix.hpp"

#include <cassert>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "cost_matrix_integer.hpp"
#include "edge.hpp"
#include "graph/graph.hpp"
#include "matrix.hpp"

using std::for_each;
using std::min_element;
using std::unordered_map;
using std::vector;

using CostVector = CostMatrix::CostVector;
using CostRow = CostMatrix::CostRow;
using CostColumn = CostMatrix::CostColumn;

static unordered_map<int, int> MakeVectorMapping(const vector<bool>& available);

CostMatrix::CostMatrix(const Graph& graph, const vector<Edge>& include,
		const vector<Edge>& exclude) {
	// parse included edges first
	vector<bool> row_available(graph.GetNumVertices(), true);
	vector<bool> column_available(graph.GetNumVertices(), true);
	for (const Edge& e : include) {
		row_available[e.u] = false;
		column_available[e.v] = false;
	}

	// We'll just skip adding the rows/columns that are unavailable to the
	// actual Matrix data structure to save space. Call this the "condensed
	// matrix". Make a mapping of actual row/column numbers to condensed matrix 
	// row/column numbers
	row_mapping_ = MakeVectorMapping(row_available);
	column_mapping_ = MakeVectorMapping(column_available);
	cost_matrix_.SetSize(row_mapping_.size(), column_mapping_.size());

	// create the condensed matrix
	for (int i{0}; i < graph.GetNumVertices(); ++i) {
		if (!IsRowAvailable(i)) { continue; }
		for (int j{0}; j < graph.GetNumVertices(); ++j) {
			if (!IsColumnAvailable(j)) { continue; }
			(*this)(i, j) = CostMatrixInteger{graph(i, j), Edge{i, j}};
		}
	}

	// mark cells that have been excluded as infinite
	for (const Edge& e : exclude) {
		if (!IsRowAvailable(e.u) || !IsColumnAvailable(e.v)) { continue; }
		(*this)(e.u, e.v).SetInfinite();
	}
}

int CostMatrix::ReduceMatrix() {
	// keep track of the amount reduced off the matrix
	int decremented{0};

	// reduce all the rows
	for (int row_num{0}; row_num < Size(); ++row_num) {
		CostRow cost_row{&cost_matrix_, row_num};
		auto min_it = min_element(cost_row.begin(), cost_row.end());
		assert(min_it != cost_row.end());
		CostMatrixInteger min{*min_it};
		assert(!min.IsInfinite());
		for_each(cost_row.begin(), cost_row.end(),
				[&min](CostMatrixInteger& elt) { elt -= min; });
		decremented += min();
	}

	// reduce all the columns
	for (int column_num{0}; column_num < Size(); ++column_num) {
		CostColumn cost_column{&cost_matrix_, column_num};
		auto min_it = min_element(cost_column.begin(), cost_column.end());
		CostMatrixInteger min{*min_it};
		assert(!min.IsInfinite());
		for_each(cost_column.begin(), cost_column.end(),
				[&min](CostMatrixInteger& elt) { elt -= min; });
		decremented += min();
	}

	return decremented;
}

const CostMatrixInteger& CostMatrix::operator()(int row_num,
		int column_num) const { return operator()(Edge{row_num, column_num}); }
const CostMatrixInteger& CostMatrix::operator()(const Edge& e) const
{ return cost_matrix_(GetCondensedRowNum(e.u), GetCondensedColumnNum(e.v)); }

CostMatrixInteger& CostMatrix::operator()(int row_num, int column_num)
{ return operator()(Edge{row_num, column_num}); }
CostMatrixInteger& CostMatrix::operator()(const Edge& e)
{ return cost_matrix_(GetCondensedRowNum(e.u), GetCondensedColumnNum(e.v)); }


CostRow CostMatrix::GetRow(int row_num)
{ return CostRow{&cost_matrix_, GetCondensedRowNum(row_num)}; }

CostColumn CostMatrix::GetColumn(int column_num)
{ return CostColumn{&cost_matrix_, GetCondensedColumnNum(column_num)}; }

int CostMatrix::GetCondensedRowNum(int row_num) const {
	if (!IsRowAvailable(row_num))
	{ throw NotAvailableError{"This row number is not available"}; }
	return row_mapping_.at(row_num);
}

int CostMatrix::GetCondensedColumnNum(int column_num) const {
	if (!IsColumnAvailable(column_num))
	{ throw NotAvailableError{"This column number is not available"}; }
	return column_mapping_.at(column_num);
}

const CostMatrixInteger& CostVector::operator[](int cell_num) const
{ return (*cost_matrix_ptr_)(GetRow(cell_num), GetColumn(cell_num)); }

CostMatrixInteger& CostVector::operator[](int cell_num)
{ return (*cost_matrix_ptr_)(GetRow(cell_num), GetColumn(cell_num)); }

CostMatrixInteger& CostVector::Iterator::operator*() {
	assert(cost_vector_ptr_);
	return (*cost_vector_ptr_)[traversing_cell_index_];
}

CostMatrixInteger* CostVector::Iterator::operator->() {
	assert(cost_vector_ptr_);
	return &(*cost_vector_ptr_)[traversing_cell_index_];
}

CostVector::Iterator CostVector::Iterator::operator++(int) {
	int current_traversing_cell_index{traversing_cell_index_};
	if (traversing_cell_index_ < cost_vector_ptr_->Size())
	{ ++traversing_cell_index_; }
	return Iterator{cost_vector_ptr_, current_traversing_cell_index};
}

CostVector::Iterator& CostVector::Iterator::operator++() {
	if (traversing_cell_index_ < cost_vector_ptr_->Size())
	{ ++traversing_cell_index_; }
	return *this;
}

bool CostVector::Iterator::operator==(const CostVector::Iterator& other) const {
	return cost_vector_ptr_ == other.cost_vector_ptr_ &&
		traversing_cell_index_ == other.traversing_cell_index_;
}

bool CostVector::Iterator::operator!=(const CostVector::Iterator& other) const
{ return !(operator==(other)); }

CostColumn::CostColumn(Matrix<CostMatrixInteger>* cost_matrix_ptr,
	int column_num) : CostVector{cost_matrix_ptr}, column_num_{column_num} {}

CostRow::CostRow(Matrix<CostMatrixInteger>* cost_matrix_ptr, int row_num) :
		CostVector{cost_matrix_ptr}, row_num_{row_num} {}

CostMatrixInteger& CostMatrix::Iterator::operator*()
{ return (*cost_matrix_ptr_)(row_num_, column_num_); }

CostMatrixInteger* CostMatrix::Iterator::operator->()
{ return &(*cost_matrix_ptr_)(row_num_, column_num_); }

CostMatrix::Iterator CostMatrix::Iterator::operator++(int) {
	int current_row_num{row_num_};
	int current_column_num{column_num_};
	MoveToNextCell();
	return Iterator{cost_matrix_ptr_, current_row_num, current_column_num};
}

CostMatrix::Iterator& CostMatrix::Iterator::operator++() {
	MoveToNextCell();
	return *this;
}

bool CostMatrix::Iterator::operator==(const CostMatrix::Iterator& other) {
	return cost_matrix_ptr_ == other.cost_matrix_ptr_ &&
		column_num_ == other.column_num_ && row_num_ == other.row_num_;
}

bool CostMatrix::Iterator::operator!=(const CostMatrix::Iterator& other)
{ return !(operator==(other)); }

void CostMatrix::Iterator::MoveToNextCell() {
	// check if at end of matrix
	if (row_num_ == cost_matrix_ptr_->GetNumRows() && column_num_ == 0)
	{ return; }

	// update row and column number, so column_num_ wraps around
	column_num_ = (column_num_ + 1) % cost_matrix_ptr_->GetNumColumns();
	if (column_num_ == 0) { ++row_num_; }
}

unordered_map<int, int> MakeVectorMapping(const vector<bool>& available) {
	int condensed_matrix_current_cell{0};
	unordered_map<int, int> mapping;

	// make mapping "actual row/column" => "condensed matrix row/column"
	for (int cell_num{0}; cell_num < int(available.size()); ++cell_num) {
		if (!available[cell_num]) { continue; }
		mapping[cell_num] = condensed_matrix_current_cell++;
	}
	return mapping;
}
