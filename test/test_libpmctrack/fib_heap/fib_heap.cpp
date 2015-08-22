/**
 * fib_heap.cpp
 *
 * This is the implementation file for the fib_heap ADT.
 * A fibonacci heap is a set of heaps, not in order, which tries to get the best
 * amortized cost.
 * 
 * In this implementation, the set of heaps are stored as circular linked lists.
 * We implement the basic functions for a heap: getMin, insert, deleteMin, join and decreaseKey.
 * Also this file make use of some auxiliar operations, some are part of the fib_heap class, some others are not.
 * 		->Auxiliar operations out of the fib_heap class: 
 * 		remove_from_list, swap, concatenate_circular_lists, construct_atomic_node, printListOfListsDFS
 * 		->Auxiliar operations in the fib_heap class:
 * 		fib_heap_link, consolidate, cut, cascading_cut, operator <<
 *  
 * The costs to accomplish for each operation are defined in the according header file.
 *
 ******************************************************************************
 *
 ******************************************************************************
 * 
 *  Created on: 21-11-2014
 *      Author: Abel Serrano Juste
 **/

#include <vector>	//std::vector
#include <string>	//std::string and std::toString
#include <cmath>	//std::log2

#undef DEBUG

/* Making root_list an alias for min. This is just for making prettier the calls with min. */
#define root_list min 

namespace fibh {
	
#ifdef DEBUG 
#define D(x) (x)
#else 
#define D(x)
#endif
 
	/*****auxiliar methods*****/

/**
 * Changes pointers next to elem so that elem is not linked anymore in the list.
 * @param: @toRemove is a pointer to the node in the list to be removed
 * Cost: O(1)
 * Pre: toRemove has to be on the list. At least the list has got one element.
 * Post: list without elem or nullptr if it had only elem
 **/
void remove_from_list(node_t* &parent_list, node_t* toRemove)
{
	if (parent_list == nullptr)	//for list size==0
		throw "Trying to delete a node from an empty list";
	
	else if (parent_list == parent_list->right) //for list size==1
	{
		parent_list = nullptr;
	}
	else 					//for list size >1
	{
		if (parent_list == toRemove)
			parent_list = toRemove->right;
		toRemove->left->right = toRemove->right;
		toRemove->right->left = toRemove->left;
	}
}

/**
 * After this, x will point to y and y will point to x
 * Cost: O(1)
 **/
void swap(node_t*& x, node_t*& y)
{
	node_t *tmp;
	tmp = x;
	x = y;
	y = tmp;
}

/**
 * list1 will be the concatenation (append list2 to list1). It also returns list1.
 * Attention!! This does not create a new list, instead, it puts the output list in list1. to make it more efficient.
 * Cost: O(1)
 * Pre: two lists: list1 & list2
 * Post: list1 which is union of list1,list2
 */
node_t* concatenate_circular_lists(node_t* &list1, node_t* &list2)
{
	if (list1 == nullptr)
		return (list1 = list2);
		
	if (list2 == nullptr)
	{
		return list1;
	}
	
	//Getting tails for each list
	node_t *tail1 = list1->left;
	node_t *tail2 = list2->left;
	
	/*  hooking list1 with list2  */
	//setting list2 head->previous and list2 tail->next
	list2->left = tail1;
	tail2->right = list1;
	//Now, setting list1 head->previous and list1 tail->next
	list1->left = tail2;
	tail1->right = list2;
	
	return list1;
}

/**
 * Create a new node_t and fills every field of node_t with x and by default values.
 * Default values are: degree = 0; child and parent are nullptr; 
 * 	mark is false; and left and right are this node itself
 * Pre: integer x which will be the value of the key
 * Post: node_t containing x as key, and every other field filled by default.
 **/
node_t* construct_atomic_node(const int x)
{
	node_t* node_out = new node_t;
	
	node_out->key = x;
	
	node_out->degree = 0;
	node_out->parent = node_out->child = nullptr;
	node_out->left = node_out->right = node_out;
	node_out->mark = false;	
	
	return node_out;
}

/**
 * Get list of node_t (which can possibly contain sublists respectively and those sublists another sublists and so on)
 *   and return a vector of strings where each string represents one level in the list of lists.
 * This function is recursive and it's called while the list has any sublist.
 * Cost: O (n)
 * Pre: a non-empty list (list != nullptr)
 * Post: ret vector of strings with 'lvl' is no of strings where each string represents one lvl of depth of the lists
 * 
 **/
void printListOfListsDFS( node_t* list, std::vector<std::string> &ret, unsigned int lvl )
{
	node_t* it = list;
	
	while (lvl >= ret.size()) //Typically, it will execute only once, I've put a while just to support lvl > size +1 cases
		ret.emplace_back( std::string("") ); //C++11 feature: creates new string object and push it back to ret
		
	if (list->parent != nullptr)
		ret[lvl] += ("(" + std::to_string(list->parent->key) + " ->)");
	
	do
	{
		ret[lvl] += std::to_string(it->key);
		ret[lvl] += " -> ";
		if (it->child != nullptr)
			printListOfListsDFS(it->child,ret,lvl+1);
		it = it->right;
		
	} while(it != list);
	
	ret[lvl] += "| ";
	
}

	/*****fib_heap private methods*****/

std::ostream& operator<<(std::ostream& strm, const fib_heap& f)
{
	strm << "Printing fibonacci heap..." << std::endl;
	
	std::vector<std::string> output_vector;

	unsigned int lvl = 0;
	
	//write output in DFS fashion
	if (!f.isEmpty())
		printListOfListsDFS(f.min,output_vector,lvl); 
		
	for ( unsigned int i = 0; i<output_vector.size(); ++i)
	{
		strm << output_vector[i] << std::endl;
	}
	
	return strm << "The number of elements in the heap is: " << f.n;
}

/**
 * new_child will be child of new_parent
 * Pre: new_child and new_parent are circular lists
 * Post: new_child is a child of new_parent.
 * Cost: O(1)
 **/
void fib_heap::fib_heap_link(node_t* new_child, node_t* new_parent)
{	
	
	concatenate_circular_lists(new_parent->child, new_child);
	++new_parent->degree;
	new_child->parent = new_parent;
	new_child->mark = false;
	
}

/**
 * Transform the root list into a list of binonmial heaps. 
 * It makes the amortized cost so good for a secuence of mixed insert() and deleteMin() calls.
 * Cost: O(log n)
 * Pre: this fib_heap has at least 1 element.
 * Post: list of binomial heaps
 **/
void fib_heap::consolidate()
{
	const int max_bin_heaps = 1.5 * (log2(n) + 1) ; //1.5*log n
	
	node_t* aux_array[max_bin_heaps];
	
	//Init array:
	for (int i = 0; i<max_bin_heaps; ++i)
	{
		aux_array[i] = nullptr;
	}
	
	node_t *aux1, *aux2;	//correspondence with MAR notes x <=> aux1 & y <=> aux2 
	D(std::cerr << "Debug: consolidate loop starts" << std::endl);
	
	//We've gotta go over every node in the root list
	while (root_list != nullptr)
	{
		aux1 = root_list;
		
		remove_from_list(root_list,aux1);
		
		//Make aux1 an unitary circular list (link to itself only)
		aux1->right = aux1;
		aux1->left = aux1;
		
		//Inside this while, we make sure we are creating only one bin heap of every degree
		while (aux_array[aux1->degree] != nullptr)
		{
			D(std::cerr << "bin heap of degree " << aux1->degree << " already taken by " << aux_array[aux1->degree]->key << std::endl);
			D(std::cerr << "consolidate loop" << std::endl << std::endl);
			
			aux2 = aux_array[aux1->degree]; //Another node with the same degree as aux1
			
			//if aux1 is greater, it swaps them so that aux1 is always <= aux2
			if (aux1->key > aux2->key)
			{
				swap(aux1,aux2);
			}
			
			aux_array[aux1->degree] = nullptr;
			fib_heap_link(aux2,aux1);
		}
		aux_array[aux1->degree] = aux1;
		
	}
	
	//Now, we restore the heap and the min pointer
	for (int i = 0; i<max_bin_heaps; ++i)
	{
		node_t *x = aux_array[i];
		if (x != nullptr)
		{
			if (this->isEmpty())
			{
				root_list = x;
			}
			else
			{
				concatenate_circular_lists(root_list,x);
				
				if (x->key < min->key)
					min = x;
			} 
		}
	}
	
	D(std::cerr << "Debug: consolidate ok" << std::endl);
	D(std::cout << *this << std::endl);
	
}

/**
 * x is moved to the root list.
 * Pre: x has to be in this fib_heap instance
 * Post: x is in the root list and it's marked as false.
 * Cost: O(1)
 **/
void fib_heap::cut(node_t* x)
{
	if (x->parent != nullptr)
	{
		//Unlinking from parent's children list
		remove_from_list(x->parent->child,x);
		--(x->parent->degree);
		
		//Make x an unitary circular list (link to itself). Necessary prior to call concatenate_circular_lists
		x->right = x;
		x->left = x;
		
		//Adding to root list
		concatenate_circular_lists(root_list,x);
		x->mark = false;
		x->parent = nullptr;
	}
}

/**
 * It does recursive cuts while there is a parent with a mark = true
 * Cost: O(log n)
 * Pre: node x to cut upwards
 **/
void fib_heap::cascading_cut(node_t* x)
{
	node_t* parent_x = x->parent;
	if (parent_x != nullptr)
	{
		if (x->mark == false)
			x->mark = true;
		else
		{
			cut(x);
			cascading_cut(parent_x);
		}
	}
}

	/*****public fib_heap methods*****/

/**
 * default constructor
 */
fib_heap::fib_heap()
{
	min = nullptr;
	n = 0;
}

/**
 * default destructor
 * Cost: O(nlogn)
**/
fib_heap::~fib_heap()
{
	D(std::cerr << "Debug: executing destructor" << std::endl);
	while (n > 0)
	{
		D(std::cerr << "Debug: Deleting " << min->key << std::endl);
		this->deleteMin();
		D(std::cerr << *this << std::endl);
	}
	D(std::cerr << "Debug: destructor done" << std::endl);
}
	
/** 
 * returns true if empty, else returns false
 *	Cost: O(1) 
 **/
bool fib_heap::isEmpty() const
{
	return root_list == nullptr;
}
	
/** 
 * Returns elem with the lowest key from the heap.
 * 	Cost: O(1) 
 *	Pre: heap has to be non-empty 
 *  Post: integer which is the minimum value in the heap
*/
int fib_heap::getMin() const
{
	if (this->isEmpty())
		throw ("Trying to getMin of an empty heap");
	else
		return min->key;
}

/**
 * Returns number of elements in the heap
 * Cost: O(1) 
 **/
std::size_t fib_heap::size() const
{
	return this->n;
}
	
/** 
 * Insert new element in the heap
 *	Cost: O(1) 
 *	Pre: a fib_heap with n elems 
 *	Post: fib_heap with n+1 elems and a reference to the added element within the fib_heap
**/
node_t* fib_heap::insert(const int x)
{
	//Create and init new node
	node_t *new_node = construct_atomic_node(x);
	
	//Concatenate with root list
	concatenate_circular_lists(root_list,new_node);
	
	//Check min and increase counter
	++n;
	if (new_node->key < min->key)
		min = new_node;
		
	return new_node;
}
	
/** 
 * Delete the element associated with the lowest key and returns it
 * Cost: Worst case: O (n) ; Amortized: O(log n) 
 * Pre: a fib_heap with n elems 
 * Post: a fib_heap with n-1 elems where the removed elem was the min.key of previuos heap 
**/
int fib_heap::deleteMin()
{	
	if (this->isEmpty() )
	{
		throw ("Trying to deleteMin in a empty heap");
	}
	else
	{
		node_t *min_out = min;
		int min_v = min_out->key; 
		
		node_t *children_list = min_out->child;
		if (children_list != nullptr)
		{
			min_out->child = nullptr;
			min_out->degree = 0;
			
			//Setting parent for children to nullptr:
			node_t* aux = children_list;
			do
			{
				aux->parent = nullptr;
				aux = aux->right;
			}while (aux != children_list);
			
			//Append children_list to root list
			concatenate_circular_lists(root_list,children_list);
		}
			
		//Decrementing size of the fib heap
		--n;
		//Readjusting fib_heap according to the new structure and lost of min
		remove_from_list(root_list,min_out);
		
		if (n > 1)
			consolidate();

		delete min_out;
		return min_v;
	}
}
	
/**
 * Switching key of elem, pointed by pos, in new_key
 * Cost: Worst case: O (log n); amortized: O(1)
 * Pre: @pos is pointing to the elem whose key wants to be changed
 * Post: fib_heap where elem now has the new_key value
 **/
void fib_heap::decreaseKey( node_t* pos, int new_key)
{
	if (new_key >= pos->key)
		return; //DO NOTHING
	else
	{
		pos->key = new_key;
		
		node_t *pos_parent = pos->parent;
		if (pos_parent != nullptr && new_key < pos_parent->key)
		{
			cut(pos);
			cascading_cut(pos_parent);
		}
		if (pos->key < min->key)
			min = pos;
	}
}
	
/**
 * Merges fib1 and fib2, and put the result in fib1. fib1 will be a fibheap which contains elems from both input heaps
 * Attention!! This does not create a new fib_heap, instead, it puts the union heap in fib1.
 * Cost: O(1)
 * Pre: fib1 and fib2 are fib_heap
 * Post: fib1 is a fib_heap which contain every elem in (fib1 U fib2).
 * 		fib2 will become unusable */
void merge( fib_heap& fib1, fib_heap& fib2)
{
	if (fib2.isEmpty())	
		return;

	if (fib1.isEmpty())
	{
		fib1.min = fib2.min;
		fib1.n = fib2.n;
	}
	else
	{
		fib1.min = concatenate_circular_lists(fib1.min,fib2.min);
		fib1.n = fib1.n + fib2.n;
		if (fib2.min->key < fib1.min->key)
			fib1.min = fib2.min;
		
	}
	fib2.min = nullptr;
	fib2.n = 0;
}

}; //closing namespace fibh
