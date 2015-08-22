/*
 * heap.cpp
 * 
 * This is the implementation file with the c++ code for the operations for this ADT.
 * The heap is implemented with an array, following this:
 * 	For a node i, an array P:
 *	• left child is P[2i]
 *	• right child at P[2i + 1]
 *	• parent is P[i/2]
 * AND
 *  heap-propery is committed:
 * 		for every node i:
 * 			P[i] <= P[2i]
 * 			P[i] <= P[2i+1]
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
 ******************************************************************************
 *  Created on: 13-03-2014
 *      Author: Abel Serrano
 */

using namespace std;

template <class T>
heap<T>::heap()
{
	last=0;
}

template <class T>
heap<T>::heap(const heap& otherHeap)
{
	last = otherHeap.last;
	for (unsigned int i = root; i<=last; ++i)
		this->cont[i] = otherHeap.cont[i];
}

template <class T>
size_t heap<T>::size() const
{
	return last;
}

/**
 * It applies the heapify function over the subheap: check children and parent, and swap with the min. if any child is lower.
 * Pre: A subtree that is a heap for all its nodes except possibly for the root of the subheap, which is looking its place up to down
 * Post: A subtree which is a heap
 * args: @subHeapIndex: root of the subheap to heapify down from;
 */
template <class T>
void heap<T>::heapify_down(int subHeapIndex)
{
	T temp = cont[subHeapIndex];
	bool heap_property = false;
	unsigned int i = subHeapIndex;
	
	while (i<=last/2 && !heap_property)	//heapifying if we are not in the leaves and heap property is not accomplished
	{
		int min; //index of the min between left and right children
		if (!(cont[2*i]>cont[2*i+1]) || (2*i == last))	//if left is lower or equal than right or there is no right child
			min =  2*i;									//min is the left child
		else
			min = 2*i+1;								//else, min is the right child.
			
		heap_property = !(temp > cont[min]);	//heap property := temp is lower or equal than min of its children
		if (!heap_property)
		{
			cont[i] = cont[min];			//min. child go up one level
			i = min;						//to move on the index to the min. child.
		}
	}
	
	cont[i] = temp;	//Either i is a leave or we got the heap property with cont[i] and we place here the previous last.
}

/**
 * There has been a change in the heap, causing the root of a subheap to be possibly wrong with its parent.
 * 	This procedure fix whole the heap, starting from the subheap given and moving upwards, making a valid heap.
 * Pre: A subheap of the heap which is ok but possibly the root of this subheap is not right with the parent in the main heap
 * Post: Whole heap is a heap
 * args: @subHeapIndex: root of the subheap to heapify up from;
 */
template <class T>
void heap<T>::heapify_up(int subHeapIndex)
{
	T temp = cont[subHeapIndex];
	int i = subHeapIndex;
	bool heap_property = false;

	
	while (i>root && !heap_property)	//heapifying if we are not on the top and heap property is not accomplished
	{
		//heap_property = cont[i/2] < temp;
    heap_property = temp > cont[i/2];
    
		if (!heap_property)
		{
			cont[i] = cont[i/2]; //swap parent with its children
			i = i / 2;			//move index up
		}
	}
	
	cont[i] = temp;				//Found where temp is greater than its parent or has reached the top, place here.
}

/* Constructor of a heap with n elements from an array in O(n) */
template <class T>
heap<T>::heap(const T array[], const int n)
{
	/* Copying array content into the heap array container */
	last=n;
	for (int i = 0, j = root; i<n; i++, j++ )
		cont[j] = array[i];
	
	/* Variables definition */
	int parentIndex;
	
	//(We start assuming leafs (cont[last/2 +1]...cont[last]) are subheaps with just one node.
	//From there we keep making subheaps upwards, merging like this all the suheaps until we reach the top.
	//Finally we obtain a heap we all the elements of the array. This is O(n/2)
	for ( parentIndex = last / 2; parentIndex >= root; --parentIndex)
	{
		heapify_down(parentIndex);
	}
}

/*
 * Not needed anymore since we got the function argument in insert and a temp local variable in deleteMin
 * where we can compare the element we want to place until we find the right place for it. Avoiding to 
 * do swaps in the process, and therefore, improving in efficiency.
 * It's one assignment instruction the difference, but, be aware that now we are storing any data type,
 * since simple integers until long data structures, as long as they have implemented the > operator.*/
/*template <class T>
static void swap_heap(T& x, T& y)
{
	T temp = x;
	x = y;
	y = temp;
}*/

template <class T>
T heap<T>::getMin()
{
	if (last>0)
		return cont[root];
	else
		{
			cerr << "Error: Trying to get the min. from an empty heap" << endl;
			return cont[root]; // Just to avoid warning, but this will throw a runtime error. Can be removed if necessary.
		}
}


template <class T>
void heap<T>::insert(const T& x)
{
	++last;
	cont[last] = x;
	heapify_up(last);
}

template <class T>
T heap<T>::deleteMin()
{
	if (last>0)
	{
		T min = cont[root];
		
		cont[root] = cont[last];
		last--;
		
		heapify_down(root);
	
		return min;
	}
	else
	{
		throw("Error: Trying to delete the min. from an empty heap");
	}
}

template <class T>
bool heap<T>::isEmpty() const
{
	return !last;
}

template <class T>
bool heap<T>::operator==(const heap<T>& h2) const
{
	if (this->last == h2.last)
	{
		heap<T> h1_copy (*this), h2_copy(h2);
		bool equal = true;
		while (!h1_copy.isEmpty() && equal)
			equal = h1_copy.deleteMin() == h2_copy.deleteMin();
		return equal;
	}
	else
		return false;
}

template <class T>
void heap<T>::changeKey(const unsigned int i, const T new_key)
{
	if (i < 0 || i >= last) //index out of range
	{
		cerr << "Error: Trying to change the key of an out-of-range elem" << endl;
		return;
	}
	
	T old_key = this->cont[i];
	
	if (old_key == new_key)	//nothing to change
		return;
		
	else
	{
		cont[i] = new_key;
		if (new_key > old_key)
			heapify_down(i);
		else
			heapify_up(i);
	}	
}

template <class T>
std::ostream& operator<<(std::ostream& strm, const heap<T>& h)
{
	heap<T> h_local(h);
	int lvlcount = 1, nextlvl = 1;  //For printing one line per level.
	
	while (!h_local.isEmpty())
	{
		strm << h_local.deleteMin() << "   ";
		--lvlcount;
		if (lvlcount == 0)
		{
			strm << endl;
			lvlcount = nextlvl *= 2;
		}
		
	}
	strm << std::endl << "The number of elements in the heap is: " << h.size() << std::endl;
	return strm;
}
