/*
 * fib_heap.h
 *
 * This is the header (declarations) file for a personal implementation of fibonacci minheaps.
 * 
 *  A fibonacci heap is a set of heaps, not in order, which tries to get best
 * amortized cost.
 * 
 * This header file contains declarations, documentation & costs for the operations of this ADT.
 *
 ******************************************************************************
 *  Created on: 19-11-2014
 *      Author: Abel Serrano Juste
 */

#ifndef FIB_HEAP_H
#define FIB_HEAP_H

#include <cstddef>	//std::size_t
#include <iostream>	//std::ostream& operator<< and couts in .cpp

namespace fibh {
	
struct node_t{
	unsigned int degree;
	node_t *parent;
	node_t *child;
	node_t *left;
	node_t *right;
	bool mark;
	int	key;
};
	
class fib_heap
{
	public:
	
	friend struct node_t;
	
	//default constructor:
		/* O(1) */
	fib_heap();
	
	//default destructor
		/* O(nlogn) */
	~fib_heap();
	
	//returns true if empty, else returns false
		/* O(1) */
	bool isEmpty() const;
	
	//Returns elem with the lowest key from the heap.
		/* O(1) */
		/* Pre: heap has to be non-empty */
	int getMin() const;
	
	//Returns number of elements in the heap
		/* O(1) */
	std::size_t size() const;
	
	//Insert new element in the heap
		/* O(1) */
		/* Pre: a fib_heap with n elems */
		/* Post: fib_heap with n+1 elems and a reference to the added element within the fib_heap */
	node_t* insert(const int x);
	
	//Delete the elemen associated with the lowest key and returns it
		/* Worst case: O (n) ; Amortized case: O(log n) */
		/* Pre: a fib_heap with n elems */
		/* Post: a fib_heap with n-1 elems and the removed elem was the min.key of the initial heap */
	int deleteMin();
	
	//Switching key of elem, pointed by pos, in new_key
		/* Worst case: O (log n); amortized case: O(1) */
	void decreaseKey( node_t* pos, int new_key);
	
	 // Merges fib1 and fib2, and put the result in fib1. fib1 will be a fibheap which contains elems from both input heaps
	// Attention!! This does not create a new fib_heap, instead, it puts the union heap in fib1.
		/* O(1) */
		/* Pre: fib1 and fib2 are fib_heap */
		/* Post: fib1 is a fib_heap which contain every elem in (fib1 U fib2).
		 * 		fib2 will become unusable */
	friend void merge ( fib_heap& fib1, fib_heap& fib2);
	
	//for printing heap with cout
		/* O (n) */
	friend std::ostream& operator<<(std::ostream&, const fib_heap&);
	
	private:

		node_t *min;
		std::size_t n;
		
		void concatenate_with_root(node_t*);
		void consolidate();
		void cut(node_t*);
		void cascading_cut(node_t*);
		void fib_heap_link(node_t *, node_t *);
};

}

#include "fib_heap.cpp"

#endif //FIB_HEAP_H
