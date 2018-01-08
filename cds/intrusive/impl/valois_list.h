//TODO: maybe add DOXYGEN docs

#ifndef CDS_VALOIS_LIST_H
#define CDS_VALOIS_LIST_H

#include <cds/intrusive/details/valois_list_base.h>
#include <cds/details/make_const_type.h>

//CRITICAL: cheak all functions use safe read/write
//CRITICAL: cheak nullptr/NULL values for aux, Head, Tail nodes

namespace cds {
    namespace intrusive {

        template<class GC, typename T, class Traits>
        class ValoisList {

            friend class iterator;

        public:
            typedef GC gc;         ///< Garbage collector
            typedef T value_type; ///< type of value stored in the list
            typedef Traits traits;     ///< Traits template parameter
            typedef valois_list::node<value_type> node_type;  ///< node type

            typedef typename opt::details::make_comparator<value_type, traits>::type key_comparator;

            typedef typename traits::disposer disposer;            ///< disposer for \p value_type
            typedef typename traits::back_off back_off;       ///< back-off strategy
            typedef typename traits::item_counter item_counter;   ///< Item counting policy used
            typedef typename traits::memory_model memory_model;   ///< Memory ordering. See \p cds::opt::memory_model option
            typedef typename traits::node_allocator node_allocator; ///< Node allocator
            typedef typename traits::stat stat;           ///< Internal statistics

            typedef typename gc::template guarded_ptr<value_type> guarded_ptr;    ///< Guarded pointer

            static CDS_CONSTEXPR const size_t c_nHazardPtrCount = 4;    ///< Count of hazard pointer required for the algorithm

        protected:
            typedef typename atomics::atomic<node_type *> atomic_node_ptr;
            typedef typename node_type::marked_data_ptr marked_data_ptr;

            atomic_node_ptr m_pHead;        ///< Head pointer
            atomic_node_ptr m_pTail;        ///< Tail pointer
            item_counter m_ItemCounter;     ///< Item counter

            node_type *m_Head;
            node_type *m_Tail;

        protected:
            //template <bool IsConst>
            class iterator {
                friend class ValoisList;

            protected:
                node_type *m_pNode;         // Valois target - current real node
                node_type *aux_pNode;       // Valois pre_aux - aux node before the real node
                node_type *cell_pNode;      // Valois pre_cell - real node before the current real node
                typename gc::Guard m_Guard;

                bool next() {
                    if (m_pNode->next == nullptr) {     // if tail
                        return false;
                    }

                    cell_pNode->next.store( m_pNode, memory_model::memory_order_consume);
                    aux_pNode->next.store( m_pNode->next, memory_model::memory_order_consume);
                    update_iterator();
                    return true;
                }

            public:
                typedef typename cds::details::make_const_type<value_type, false>::pointer value_ptr;
                typedef typename cds::details::make_const_type<value_type, false>::reference value_ref;

                iterator()
                        : m_pNode(nullptr), aux_pNode(nullptr), cell_pNode(nullptr) {}

                iterator( node_type * node) {

                    m_pNode = new node_type();
                    aux_pNode = new node_type();
                    cell_pNode = new node_type();

                    m_pNode->next.store(NULL, memory_model::memory_order_consume);

                    aux_pNode->next.store(
                            node->next.load(memory_model::memory_order_seq_cst),
                            memory_model::memory_order_consume
                    );

                    cell_pNode->next.store(
                            node,
                            memory_model::memory_order_consume
                    );

                    update_iterator();
                }

                void update_iterator() {
                    if (aux_pNode->next == m_pNode) {
                        return;
                    }

                    node_type *p = aux_pNode;
                    node_type *n = p->next.load(atomics::memory_order_release);

                    while ( n->next != nullptr && n->data == NULL) {    //while not last and is aux node
                        cell_pNode->next.compare_exchange_strong(
                                p,
                                n,
                                memory_model::memory_order_release,
                                atomics::memory_order_relaxed
                        );
                        p = n;
                        n = p->next.load(atomics::memory_order_consume);
                    }

                    aux_pNode = p;
                    m_pNode = n;
                }

                iterator &operator++() {
                    iterator temp = *this;
                    next();
                    return temp;
                }

                iterator &operator=(iterator &second) {
                    m_pNode = second.m_pNode;
                    aux_pNode = second.aux_pNode;
                    cell_pNode = second.cell_pNode;
                    m_Guard.copy(second.m_Guard);
                    return *this;
                }

                bool operator==(const iterator &second) const {
                    return (m_pNode->data == second.m_pNode->data);
                }

                bool operator!=(const iterator &second) const {
                    return (m_pNode->data != second.m_pNode->data);
                }
            };

        public:
            ValoisList() {
                init_list();
            }

            ~ValoisList() {
                destroy();
            }

            iterator begin() {
                return iterator(m_Head);
            }

            /**
             * try insert in the position
             * @param i
             * @param val
             * @return
             */

            template <typename Q, typename Compare >
            bool search_insert(node_type * start_node, Q* val, Compare cmp) {

                std::cout << "val " << val << std::endl;

                iterator *i = new iterator(start_node);
                while (i->m_pNode->next.load() != nullptr && i->m_pNode->data.load().ptr() != NULL ) {
                    value_type *nVal = i->m_pNode->data.load( memory_model::memory_order_relaxed ).ptr();
                    //value_type *nVal = i->m_pNode->data.load().all();
                    int const nCmp = cmp(*nVal, *val);

                    if (nCmp == 0) {
                        delete i;
                        return true;
                    } else if (nCmp > 0) {
                        bool k = try_insert(i, val);
                        delete i;
                        return k;
                    } else {
                        i->next();
                    }
                }

                std::cout << "after while " << std::endl;
                bool k = try_insert(i, val);
                delete i;
                return k;
            }

            bool try_insert(iterator *i, value_type * val) {
                std::cout << "val " << val << std::endl;
                i->update_iterator();

                node_type *real_node = new node_type(val);
                std::cout << "insert  " << real_node->data.load().ptr();
                node_type *aux_node = new node_type();

                real_node->next = aux_node;
                aux_node->next = i->m_pNode;

                bool insert_status = i->aux_pNode->next.compare_exchange_strong(
                        i->m_pNode,
                        real_node,
                        memory_model::memory_order_relaxed,
                        memory_model::memory_order_relaxed
                );

                std::cout << "insert_status " << insert_status << std::endl;

                return insert_status;
            }

            bool insert( value_type &val){
                std::cout << "val " << val << std::endl;
                return search_insert( m_Head, &val, key_comparator() );
            }

            void print_all(){
                iterator * i = new iterator(m_Head);
                while(i->m_pNode->next != nullptr){
                    std::cout << i->m_pNode->data.load().ptr() << std::endl;
                    i->next();
                }
            }
            /**
             *
             * It's true delete algorithm
             * @param i
             * @return
             */

            bool erase(iterator &i) {
                if (i.cell_pNode != nullptr) {      // if not Head
                    while (true) {
                        i.update_iterator();
                        if (delete_node(i)) {
                            return true;
                        }
                        i.update_iterator();
                    }
                }
                return false;
            }

            /**
             * delete value from linked list;
             * @param value
             * @return
             */

            bool erase(value_type &value) {
                while (true) {
                    auto iter = begin();
                    //skip if element not exists
                    if (!find(iter, value))return true;
                    if (erase(iter)) return true;
                    // commented for start from begin position
                    //iter.update_iterator();
                }
            }


            /**
             * return value by integer index
             * for testing only
             * @param index
             * @return
             */
            value_type get(int index) {
                auto iter = begin();
                int current_index = 0;
                while (iter.next()) {
                    current_index++;
                    if (current_index == index) {
                        return iter->m_pNode.data;
                    } else if (current_index > index) {
                        break;
                    }
                    return NULL;
                }
            }

            template <typename Q, typename Compare >
            bool find( Q * val, Compare cmp) {
                iterator * i = new iterator( m_Head );
                while (i->m_pNode->next.load() != nullptr && i->m_pNode->data.load().ptr() != NULL ) {
                    value_type * nVal = i->m_pNode->data.load(memory_model::memory_order_release ).ptr();

                    int const nCmp = cmp( *nVal, val );

                    if ( nCmp == 0 ){
                        delete i;
                        return true;
                    }
                    else if ( nCmp > 0 ){
                        delete i;
                        return false;
                    }
                    else{
                        i->next();
                    }
                }
                delete i;
                return false;
            }

            bool find(value_type &val) {
                return find( &val, key_comparator() );
            }


            bool empty() {
                iterator * i = new iterator(m_Head);
                if (i->next()) {
                    // if next is not exist() container is empty()
                    delete i;
                    return false;
                } else {
                    delete i;
                    return true;
                }
            }

        private:

            void init_list() {
                node_type * aux_temp = new node_type();
                m_Head = new node_type();
                m_Tail = new node_type();
                m_Head->next.store( aux_temp, memory_model::memory_order_relaxed);  //link to aux node
                aux_temp->next.store( m_Tail, memory_model::memory_order_relaxed );
                m_Tail->next.store(nullptr, memory_model::memory_order_relaxed );          //link tail to nullptr
            }

            void destroy() {
                //TODO: fix destroy()
                //node_type *pNode = m_Head.next.load(memory_model::memory_order_relaxed);
                //
                //node_type *pNode = m_Head->next;
                //
                //while (pNode != pNode->next.load(memory_model::memory_order_relaxed)) {
                //    value_type *pVal = pNode->data.load(memory_model::memory_order_relaxed).ptr();
                //    if (pVal)
                //        erase(pNode);
                //    node_type *pNext = pNode->next.load(memory_model::memory_order_relaxed);
                //    pNode = pNext;
                //}
            }

            bool delete_node(iterator i) {

                node_type *for_delete = i.m_pNode;
                node_type *adjacent = i.m_pNode->next;

                bool delete_status = i.aux_pNode->next.compare_exchange_strong(for_delete, adjacent,
                                                                               memory_model::memory_order_relaxed,
                                                                               memory_model::memory_order_release);

                if (delete_status) {
                    --m_ItemCounter;
                }

                return delete_status;
            }
        };

    }
}

#endif //CDS_VALOIS_LIST_H
