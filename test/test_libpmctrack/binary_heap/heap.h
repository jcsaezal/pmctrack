/*
 * heap.h
 *
 *  This is a Minheap implementation which stores integer numbers.
 *  A minheap is a complete binary tree in which its internal nodes are
 * satisfying that every internal node in the graph stores a key
 * greater than or equal to its parent’s key.
 *
 * This is the header file with the definitions for the operations for this ADT.
 *
 * Version 1.1 :
 * 		->Made class template to contain any data type.
 * Version 1.2 :
 * Some possible improvements from the Classes & Data structures in c++ book:
 * 		->No swaps needed since we can compare the elements before to find the right place in the heap.
 * 		->Constructor of a heap with n elements from an array (no heap) in O(n)
 * 		->Some extra performance improvements.
 * Version 1.3 :
 * 		->Added funcion min, which returns top of heap
 * 		->Added security/sturdiness to the ADT making some extra checks
 * 		->Added change_key procedure: including decrease and increase keys
 * 		->Using alias 'root' for the first key where before raw '1' was used
 *
 * Note that, in order to use compound data types (struct or class) for this template
 * class, you must overload the following operators:
 * 		=			Used to assign values into the heap and to swap or move values inside the heap.
 * 		>			Used to compare which element has got a key greater than the other.
 * 		==			Used to compare two heaps or for looking for an element
 *
 ******************************************************************************
 *  Created on: 13-03-2014
 *      Author: Abel Serrano
 */

#ifndef heap_h
#define heap_h

#include <iostream>
#include <cstddef>

template <class T>
class heap
{
	public:

	//maximum elements in the heap
	const static int MAX_HEAP = 1000000;

	//default constructor:
	heap();
	//copy constructor:
	heap(const heap& otherHeap);
	//Constructor of a heap with n elements from a random array in O(n)
	heap(const T array[], const int n);

	//overloading == operator
	bool operator==(const heap<T>& h2) const;

	size_t size() const;

	//Returns min. elem from the heap.
		/* Pre: heap has to be not empty */
	T getMin();

	// Insert element to the adequate positiong inside the heap.
	// Note that the insertion will use the assignment operator. Deep copy insertions are suggested.
	void insert(const T& x);

	//Returns min. elem from the heap.
		/* Pre: heap has to be not empty */
	T deleteMin();

	bool isEmpty() const;

	void changeKey(const unsigned int index_of_elem, const T new_key);

	private:
	const static int root = 1;
	unsigned int last;
	T cont[MAX_HEAP+1];

	//Cost: log n
	void heapify_down(int subHeapIndex);

	//Cost log n
	void heapify_up(int subHeapIndex);
};

//for printing heap with cout
template <class T>
std::ostream& operator<<(std::ostream&, const heap<T>& h);

#include "heap.cpp"

#endif //heap_h
