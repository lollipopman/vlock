#include <iterator>
#include <list>

/* An edge of the graph, specifying that predecessor must come before
 * successor. */
template<class T> struct Edge {
  T predecessor;
  T successor;
};

/* For the given digraph, generate a topological sort of the nodes.
 *
 * Sorts the list and deletes all edges.  If there are circles found in the
 * graph or there are edges that have no corresponding nodes the erroneous
 * edges are left. */
template<class T> bool tsort(std::list<T>& nodes, std::list<Edge<T>*>& edges);


/* Check if the given node has no incoming edges. */
template <class T>
bool is_zero(T& node, std::list<Edge<T>*>& edges)
{
  for (typename std::list<Edge<T>*>::iterator it = edges.begin();
      it != edges.end(); it++)
    if ((*it)->successor == node)
      return false;

  return true;
}

/* Get all nodes (plugins) with no incoming edges. */
template <class T>
static std::list<T> *get_zeros(std::list<T>& nodes, std::list<Edge<T>*>& edges)
{
  std::list<T> *zeros = new std::list<T>(nodes);

  for (typename std::list<Edge<T>*>::iterator it = edges.begin();
      !zeros->empty() && it != edges.end(); it++)
    zeros->remove((*it)->successor);

  return zeros;
}


/* Sort all nodes with a topological sort.  Returns a list of sorted nodes if
 * sorting was successful and NULL if cycles were found.  If sorting was
 * successful, all edges are deleted.  If cycles were found the at least the
 * edges beloning to the cycles are left intact.
 *
 * Algorithm:
 * http://en.wikipedia.org/w/index.php?title=Topological_sorting&oldid=153157450#Algorithms
 */
template<class T> bool tsort(std::list<T>& nodes, std::list<Edge<T>*>& edges)
{
  std::list<T> *sorted_nodes = new std::list<T>;
  std::list<T> *zeros = get_zeros(nodes, edges);
  bool result;

  while (!zeros->empty()) {
    T zero = zeros->front();
    zeros->pop_front();

    sorted_nodes->push_back(zero);

    for (typename std::list<Edge<T>*>::iterator it = edges.begin();
        it != edges.end();) {
      if ((*it)->predecessor != zero) {
        it++;
        continue;
      } else {
        edges.remove(*it);

        if (is_zero((*it)->successor, edges))
          zeros->push_back((*it)->successor);

        delete *it;
      }
    }
  }

  result = edges.empty();

  if (result)
    nodes.assign(sorted_nodes->begin(), sorted_nodes->end());

  delete sorted_nodes;
  return result;
}
