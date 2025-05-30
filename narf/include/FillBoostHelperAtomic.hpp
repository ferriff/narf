#ifndef FILLBOOSTHELPERATOMIC_H
#define FILLBOOSTHELPERATOMIC_H

#include <boost/histogram.hpp>

#include "TROOT.h"
#include "ROOT/RDF/Utils.hxx"
#include "ROOT/RDF/ActionHelpers.hxx"
#include "histutils.hpp"

#include <iostream>
#include <array>

namespace narf {

   using namespace ROOT::Internal::RDF;
   using namespace boost::histogram;

   template <typename axes_type>
   struct is_static : std::false_type {};

   template <typename... Axes>
   struct is_static<std::tuple<Axes...>> : std::true_type {};

   template <class T, T Offset, T... Idxs>
   auto integer_sequence_offset_impl(std::integral_constant<T, Offset>, std::integer_sequence<T, Idxs...>) {
      return std::integer_sequence<T, Idxs + Offset...>{};
   }

   template<class T, T Start, T Stop>
   using make_integer_sequence_offset = decltype(integer_sequence_offset_impl(std::integral_constant<T, Start>{}, std::make_integer_sequence<T, Stop-Start>{}));

   template <std::size_t Start, std::size_t Stop>
   using make_index_sequence_offset = make_integer_sequence_offset<std::size_t, Start, Stop>;

   template<typename T, typename U>
   auto product(const T &x, const U &y) {
     return x*y;
   }

   template<typename T>
   T scalar_tensor_product_helper_impl2(const T &x, const T &y) {
      return x*y;
   }

   template<typename T, int Options_, typename IndexType, std::ptrdiff_t... Indices>
   Eigen::TensorFixedSize<T, Eigen::Sizes<Indices...>, Options_, IndexType> scalar_tensor_product_helper_impl2(const T &x, const Eigen::TensorFixedSize<T, Eigen::Sizes<Indices...>, Options_, IndexType> &y) {
      return x*y;
   }

   template<typename T, int Options_, typename IndexType, std::ptrdiff_t... Indices>
   Eigen::TensorFixedSize<T, Eigen::Sizes<Indices...>, Options_, IndexType> scalar_tensor_product_helper_impl2(const Eigen::TensorFixedSize<T, Eigen::Sizes<Indices...>, Options_, IndexType> &x, const T &y) {
      return x*y;
   }

   template<typename First, typename Second, typename... Others>
   auto const scalar_tensor_product_helper_impl(const First &first, const Second &second, const Others&... others) {
      auto const res = scalar_tensor_product_helper_impl2(first, second);
      if constexpr (sizeof...(others) == 0) {
         return res;
      }
      else {
         return scalar_tensor_product_helper_impl(res, others...);
      }
   }

   // use decltype(auto) to return as reference where possible (this is the case when there is only one weight)
   template<typename First, typename... Others>
   decltype(auto) scalar_tensor_product_helper(const First &first, const Others&... others) {
      if constexpr (sizeof...(others) == 0) {
         return first;
      }
      else {
         return scalar_tensor_product_helper_impl(first, others...);
      }
   }

   template <typename HIST, typename HISTFILL = HIST>
   class FillBoostHelperAtomic : public ROOT::Detail::RDF::RActionImpl<FillBoostHelperAtomic<HIST, HISTFILL>> {
      std::shared_ptr<HIST> fObject;
      std::shared_ptr<HISTFILL> fFillObject;

      // class which wraps a pointer and implements a no-op increment operator
      template <typename T>
      class ScalarConstIterator {
         const T *obj_;

      public:
         ScalarConstIterator(const T *obj) : obj_(obj) {}
         const T &operator*() const { return *obj_; }
         ScalarConstIterator<T> &operator++() { return *this; }
      };

      // helper functions which provide one implementation for scalar types and another for containers
      // TODO these could probably all be replaced by inlined lambdas and/or constexpr if statements
      // in c++17 or later

      // return unchanged value for scalar
      template <typename T, typename std::enable_if<!IsDataContainer<T>::value, int>::type = 0>
      ScalarConstIterator<T> MakeBegin(const T &val)
      {
         return ScalarConstIterator<T>(&val);
      }

      // return iterator to beginning of container
      template <typename T, typename std::enable_if<IsDataContainer<T>::value, int>::type = 0>
      auto MakeBegin(const T &val)
      {
         return std::begin(val);
      }

      // return 1 for scalars
      template <typename T, typename std::enable_if<!IsDataContainer<T>::value, int>::type = 0>
      std::size_t GetSize(const T &)
      {
         return 1;
      }

      // return container size
      template <typename T, typename std::enable_if<IsDataContainer<T>::value, int>::type = 0>
      std::size_t GetSize(const T &val)
      {
   #if __cplusplus >= 201703L
         return std::size(val);
   #else
         return val.size();
   #endif
      }

      template <typename A, typename T, T... Idxs, T... WeightIdxs>
      void FillHist(const A &tup, std::integer_sequence<T, Idxs...>, std::integer_sequence<T, WeightIdxs...>) {
         using namespace boost::histogram;
         // weird type issues if argument to weight is an rval, so store the temporary here
         // FIXME understand and fix this
         auto const wgtval = scalar_tensor_product_helper(std::get<WeightIdxs>(tup)...);
         (*fFillObject)(std::get<Idxs>(tup)..., weight(wgtval));
      }

      template <typename A, typename T, T... Idxs, T... WeightIdxs>
      void FillHistIt(const A &tup, std::integer_sequence<T, Idxs...>, std::integer_sequence<T, WeightIdxs...>) {
         using namespace boost::histogram;
         // weird type issues if argument to weight is an rval, so store the temporary here
         // FIXME understand and fix this
         auto const wgtval = scalar_tensor_product_helper(*std::get<WeightIdxs>(tup)...);
         (*fFillObject)(*std::get<Idxs>(tup)..., weight(wgtval));
      }

      template <std::size_t ColIdx, typename End_t, typename... Its>
      void ExecLoop(unsigned int slot, End_t end, Its... its)
      {
         constexpr auto N = sizeof...(its);
         constexpr auto rank = std::tuple_size<typename HISTFILL::axes_type>::value;

         // loop increments all of the iterators while leaving scalars unmodified
         // TODO this could be simplified with fold expressions or std::apply in C++17
         auto nop = [](auto &&...) {};
         for (auto itst = std::forward_as_tuple(its...); std::get<ColIdx>(itst) != end; nop(++its...)) {
            if constexpr (N > rank) {
               // fill with weight
               FillHistIt(itst, std::make_index_sequence<rank>{}, make_index_sequence_offset<rank, N>{});
            }
            else {
               // fill without weight
               (*fFillObject)(*its...);
            }
         }
      }

   public:
      using Result_t = HIST;

      FillBoostHelperAtomic(FillBoostHelperAtomic &&) = default;
      FillBoostHelperAtomic(const FillBoostHelperAtomic &) = delete;

      FillBoostHelperAtomic(HIST &&h) : fObject(std::make_shared<HIST>(std::move(h))) {
         if constexpr(std::is_same_v<HIST, HISTFILL>) {
            fFillObject = fObject;
         }

         if (ROOT::IsImplicitMTEnabled() && !HISTFILL::storage_type::has_threading_support) {
            throw std::runtime_error("multithreading is enabled but histogram is not thread-safe, not currently supported");
         }
      }

      FillBoostHelperAtomic(HIST &&h, HISTFILL &&hfill) : fObject(std::make_shared<HIST>(std::move(h))),
      fFillObject(std::make_shared<HISTFILL>(std::move(hfill))) {

         if (ROOT::IsImplicitMTEnabled() && !HISTFILL::storage_type::has_threading_support) {
            throw std::runtime_error("multithreading is enabled but histogram is not thread-safe, not currently supported");
         }
      }

      template <typename M>
      FillBoostHelperAtomic(const M &model, HISTFILL &&hfill) : fObject(model.GetHistogram()),
      fFillObject(std::make_shared<HISTFILL>(std::move(hfill))) {

         if constexpr (std::is_base_of_v<TH1, HIST>) {
            fObject->SetDirectory(nullptr);
         }

         if (ROOT::IsImplicitMTEnabled() && !HISTFILL::storage_type::has_threading_support) {
            throw std::runtime_error("multithreading is enabled but histogram is not thread-safe, not currently supported");
         }
      }

      void Initialize() {}

      void InitTask(TTreeReader *, unsigned int slot) {}



      // no container arguments
      template <typename... ValTypes,
               typename std::enable_if<!std::disjunction<IsDataContainer<ValTypes>...>::value, int>::type = 0>
      void Exec(unsigned int slot, const ValTypes &...x) {
         constexpr auto N = sizeof...(x);
         constexpr auto rank = std::tuple_size<typename HISTFILL::axes_type>::value;

         if constexpr (N > rank) {
            // fill with weight
            const auto xst = std::forward_as_tuple(x...);
            FillHist(xst, std::make_index_sequence<rank>{}, make_index_sequence_offset<rank, N>{});
         }
         else {
            // fill without weight
            (*fFillObject)(x...);
         }

      }

      // at least one container argument
      template <typename... Xs, typename std::enable_if<std::disjunction<IsDataContainer<Xs>...>::value, int>::type = 0>
      void Exec(unsigned int slot, const Xs &...xs)
      {
         // array of bools keeping track of which inputs are containers
         constexpr std::array<bool, sizeof...(Xs)> isContainer{IsDataContainer<Xs>::value...};

         // index of the first container input
         constexpr std::size_t colidx = FindIdxTrue(isContainer);
         // if this happens, there is a bug in the implementation
         static_assert(colidx < sizeof...(Xs), "Error: index of collection-type argument not found.");

         // get the end iterator to the first container
         auto const xrefend = std::end(std::get<colidx>(std::forward_as_tuple(xs...)));

         // array of container sizes (1 for scalars)
         std::array<std::size_t, sizeof...(xs)> sizes = {{GetSize(xs)...}};

         for (std::size_t i = 0; i < sizeof...(xs); ++i) {
            if (isContainer[i] && sizes[i] != sizes[colidx]) {
               throw std::runtime_error("Cannot fill histogram with values in containers of different sizes.");
            }
         }

         ExecLoop<colidx>(slot, xrefend, MakeBegin(xs)...);
      }

      void Finalize() {
         using acc_t = typename HISTFILL::storage_type::value_type;
//          using acc_t = std::decay_t<decltype(*fFillObject->begin())>;
//          static_assert(std::is_same_v<acc_t, typename HISTFILL::storage_type::value_type>);
         using acc_trait = narf::acc_traits<acc_t>;

         constexpr bool is_weighted_sum = acc_trait::is_weighted_sum;

         constexpr bool isTH1 = std::is_base_of<TH1, HIST>::value;
         constexpr bool isTHn = std::is_base_of<THnBase, HIST>::value;

         if constexpr (isTH1 || isTHn) {

            if constexpr (is_weighted_sum) {
               fObject->Sumw2();
            }

            if constexpr(acc_trait::is_tensor) {
               auto constexpr tensor_rank = acc_t::rank;
               const auto fillrank = fFillObject->rank();

               const auto rank = fillrank + tensor_rank;

//                std::cout << "fillrank = " << fillrank << " rank = " << rank << " tensor_rank = " << tensor_rank << std::endl;

               std::vector<int> idxs(rank);
               for (auto&& x: indexed(*fFillObject, coverage::all)) {
                  for (std::size_t idim = 0; idim < fillrank; ++idim) {
                     // convert from boost to root numbering convention
                     idxs[idim] = x.index(idim) + 1;
                  }

                  auto const &tensor_acc_val = *x;

                  for (auto it = tensor_acc_val.indices_begin(); it != tensor_acc_val.indices_end(); ++it) {
                     const auto tensor_indices = it.indices;
                     for (std::size_t idim = fillrank; idim < rank; ++idim) {
                        // convert from zero-indexing to root numbering convention
                        idxs[idim] = tensor_indices[idim - fillrank] + 1;
                     }

                     auto const &acc_val = std::apply(tensor_acc_val.data(), tensor_indices);

                     // TODO use overloaded functions to avoid switch on histogram type here
                     if constexpr (isTH1) {
                        const int i = idxs[0];
                        const int j = idxs.size() > 1 ? idxs[1] : 0;
                        const int k = idxs.size() > 2 ? idxs[2] : 0;
                        const auto bin = fObject->GetBin(i, j, k);
                        if constexpr (is_weighted_sum) {
                           fObject->SetBinContent(bin, acc_val.value());
                           fObject->SetBinError(bin, std::sqrt(acc_val.variance()));
                        }
                        else {
                           fObject->SetBinContent(bin, *x);
                        }
                     }
                     else if constexpr (isTHn) {
                        const auto bin = fObject->GetBin(idxs.data());
                        if constexpr (is_weighted_sum) {
                           fObject->SetBinContent(bin, acc_val.value());
                           fObject->SetBinError2(bin, acc_val.variance());
                        }
                        else {
                           fObject->SetBinContent(bin, acc_val);
                        }
                     }
                  }
               }
            }
            else {
               const auto rank = fFillObject->rank();
               std::vector<int> idxs(rank, 0);
               for (auto&& x: indexed(*fFillObject, coverage::all)) {

                  for (unsigned int idim = 0; idim < rank; ++idim) {
                     // convert from boost to root numbering convention
                     idxs[idim] = x.index(idim) + 1;
                  }

                  // TODO use overloaded functions to avoid switch on histogram type here
                  if constexpr (isTH1) {
                     const int i = idxs[0];
                     const int j = idxs.size() > 1 ? idxs[1] : 0;
                     const int k = idxs.size() > 2 ? idxs[2] : 0;
                     const auto bin = fObject->GetBin(i, j, k);
                     if constexpr (is_weighted_sum) {
                        fObject->SetBinContent(bin, x->value());
                        fObject->SetBinError(bin, std::sqrt(x->variance()));
                     }
                     else {
                        fObject->SetBinContent(bin, *x);
                     }
                  }
                  else if constexpr (isTHn) {
                     const auto bin = fObject->GetBin(idxs.data());
                     if constexpr (is_weighted_sum) {
                        fObject->SetBinContent(bin, x->value());
                        fObject->SetBinError2(bin, x->variance());
                     }
                     else {
                        fObject->SetBinContent(bin, *x);
                     }
                  }
               }
            }
            fFillObject.reset();
         }
         else if constexpr (is_array_interface_view<HIST>::value) {
            fObject->from_boost(*fFillObject);
            fFillObject.reset();
         }
      }

      std::shared_ptr<HIST> GetResultPtr() const {
         return fObject;
      }

      FillBoostHelperAtomic(const std::shared_ptr<HIST> & h)
        : fObject(h)
	{
         if constexpr(std::is_same_v<HIST, HISTFILL>) {
            fFillObject = fObject;
         }

         if (ROOT::IsImplicitMTEnabled() && !HISTFILL::storage_type::has_threading_support) {
            throw std::runtime_error("multithreading is enabled but histogram is not thread-safe, not currently supported");
         }
	}
      FillBoostHelperAtomic MakeNew(void * newRes, std::string_view variation = "nominal")
      {
                auto & res = *static_cast<std::shared_ptr<Result_t> *>(newRes);
                return FillBoostHelperAtomic(res);
      }

      std::string GetActionName() { return "FillBoost"; }
   };

}

#endif
