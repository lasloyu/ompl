/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2013, Autonomous Systems Laboratory, Stanford University
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of Stanford University nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/* Authors: Ashley Clark (Stanford) and Wolfgang Pointner (AIT) */
/* Co-developers: Brice Rebsamen (Stanford) and Tim Wheeler (Stanford) */
/* Algorithm design: Lucas Janson (Stanford) and Marco Pavone (Stanford) */
/* Acknowledgements for insightful comments: Edward Schmerling (Stanford), 
 * Oren Salzman (Tel Aviv University), Joseph Starek (Stanford), and Evan Clark (Stanford) */

#ifndef OMPL_GEOMETRIC_PLANNERS_FMT_
#define OMPL_GEOMETRIC_PLANNERS_FMT_

#include <ompl/geometric/planners/PlannerIncludes.h>
#include <ompl/base/goals/GoalSampleableRegion.h>
#include <ompl/datastructures/NearestNeighbors.h>
#include <ompl/datastructures/BinaryHeap.h>
#include <ompl/base/OptimizationObjective.h>
#include <map>


namespace ompl 
{

    namespace geometric 
    {

        /**
           @anchor gFMT
           @par Short description
           \ref gFMT "FMT*" is an asymptotically-optimal sampling-based motion 
            planning algorithm, which is guaranteed to converge to a shortest 
            path solution. The algorithm is specifically aimed at solving complex 
            motion planning problems in high-dimensional configuration spaces. 
            The \ref gFMT "FMT*" algorithm essentially performs a lazy dynamic 
            programming recursion on a set of probabilistically-drawn samples to 
            grow a tree of paths, which moves steadily outward in cost-to-come space. 
           @par External documentation
           L. Janson and M. Pavone, Fast Marching Trees: a Fast Marching 
           Sampling-Based Method for Optimal Motion Planning in Many Dimensions 
           -- Extended Version, International Symposium on Robotics Research, 2013.
           <a href="http://arxiv.org/pdf/1306.3532v2.pdf">http://arxiv.org/pdf/1306.3532v2.pdf</a>
        */
        /** @brief Asymptotically Optimal Fast Marching Tree algorithm developed 
            by L. Janson and M. Pavone. */
        class FMT : public ompl::base::Planner
        {
        public:
            
            FMT(const base::SpaceInformationPtr &si);
            
            virtual ~FMT(void);

            virtual void setup(void);

            virtual base::PlannerStatus solve(const base::PlannerTerminationCondition &ptc);

            virtual void clear(void);

            virtual void getPlannerData(base::PlannerData &data) const;
            
            /** \brief Set the number of states that the planner should sample.
                The planner will sample this number of states in addition to the
                initial states. If any of the goal states are not reachable from
                the randomly sampled states, those goal states will also be
                added. The default value is 1000 */
            void setNumSamples(const unsigned int numSamples)
            {
                numSamples_ = numSamples;
            }
            
            /** \brief Get the number of states that the planner will sample */
            unsigned int getNumSamples(void) const
            {
                return numSamples_;
            }
            
            /** \brief The planner searches for neighbors of a node within a
                cost r, where r is the value described for PRM* in Section
                3.3 of [S. Karaman and E. Frazzoli. "Sampling-based algorithms 
                for optimal motion planning." International Journal of Robotics 
                Research, 30(7):846–894, 2011]. The user should choose a 
                constant multiplier for the search radius that is greater than 
                one. The default value is 1.1 */
            void setRadiusMultiplier(const double radiusMultiplier)
            {
                if ( radiusMultiplier <= 1.0 )
                    throw Exception("Radius multiplier must be greater than one");
                radiusMultiplier_ = radiusMultiplier;
            }
            
            /** \brief Get the multiplier used for the nearest neighbors search
                radius */
            double getRadiusMultiplier(void) const
            {
                return radiusMultiplier_;
            }
            
            /** \brief Store the fraction of the state space that is occupied
                by obstacles. If no value is specified, the default is 0.0 */
            void setObstacleCoverage(const double obstacleCoverage)
            {
                if ( obstacleCoverage < 0.0 || obstacleCoverage > 1.0 )
                    throw Exception("Obstacle coverage must be in range [0.0,1.0]");
                obstacleCoverage_ = obstacleCoverage;
            }
            
            /** \brief Get the percentage of the state space that the planner
                considers occupied by obstacles */
            double getObstacleCoverage(void) const
            {
                return obstacleCoverage_;
            }
            
        protected:
            /** \brief Representation of a motion

                Cannot be copied because a state cannot be copied
              */
            class Motion : boost::noncopyable
            {
                public:
                    
                    /** \brief The FMT* planner begins with all nodes included in 
                        set W "Waiting for optimal connection". As nodes are 
                        connected to the tree, they are transferred into set H 
                        "Horizon of explored tree." Once a node in H is no longer 
                        close enough to the frontier to connect to any more nodes in 
                        W, it is removed from H. These three SetTypes are flags 
                        indicating which set the node belongs to; H, W, or neither */
                    enum SetType { SET_NULL, SET_H, SET_W };

                    Motion(const base::OptimizationObjectivePtr opt)
                        : state_(NULL), parent_(NULL), cost_(0.0), currentSet_(SET_NULL), children_(new std::vector<Motion*>), opt_(opt)
                    {
                    }

                    /** \brief Constructor that allocates memory for the state */
                    Motion(const base::OptimizationObjectivePtr opt, const base::SpaceInformationPtr &si)
                        : state_(si->allocState()), parent_(NULL), cost_(0.0), currentSet_(SET_NULL), children_(new std::vector<Motion*>), opt_(opt)
                    {
                    }

                    ~Motion(void)
                    {
                        delete children_;
                    }

                    /** \brief Set the state associated with the motion */
                    inline void setState(base::State * state)
                    {
                        state_ = state;
                    }
                    
                    /** \brief Get the state associated with the motion */
                    inline base::State *getState(void) const
                    {
                        return state_;
                    }
                    
                    /** \brief Set the parent motion of the current motion */
                    inline void setParent(Motion *parent)
                    {
                        parent_ = parent;
                    }
                    
                    /** \brief Get the parent motion of the current motion */
                    inline Motion *getParent(void) const
                    {
                        return parent_;
                    }
                    
                    /** \brief Set the cost-to-come for the current motion */
                    inline void setCost(const base::Cost cost)
                    {
                        cost_ = cost;
                    }
                    
                    /** \brief Get the cost-to-come for the current motion */
                    inline base::Cost getCost(void) const
                    {
                        return cost_;
                    }
                    
                    /** \brief Set the child motions of the current motion */
                    inline void setChildren(std::vector<Motion*> *children)
                    {
                        children_ = children;
                    }
                    
                    /** \brief Get the child motions of the current motion */
                    inline std::vector<Motion*> *getChildren(void) const
                    {
                        return children_;
                    }

                    /** \brief Specify the set that this motion belongs to */
                    inline void setSetType(const SetType currentSet)
                    {
                        currentSet_ = currentSet;
                    }
                    
                    /** \brief Get the set that this motion belongs to */
                    inline SetType getSetType(void) const
                    {
                        return currentSet_;
                    }
                    
                    /** \brief Get the optimization objective used to determine
                        the cost between two states */
                    inline const base::OptimizationObjectivePtr getOptimizationObjective(void) const
                    {
                        return opt_;
                    }
                    
                protected:
                        
                    /** \brief The state contained by the motion */
                    base::State                                 *state_;

                    /** \brief The parent motion in the exploration tree */
                    Motion                                     *parent_;

                    /** \brief The cost of this motion */
                    base::Cost                                    cost_;

                    /** \brief The set of motions descending from the current motion */
                    std::vector<Motion*>                     *children_;

                    /** \brief The flag indicating which set a motion belongs to */
                    SetType                                 currentSet_;
                    
                    /** \brief The cost function optimization objective */
                    const base::OptimizationObjectivePtr           opt_;
            };
            
            /** \brief Comparator used to order motions in a binary heap */
            struct MotionCompare 
            {
                MotionCompare()
                {
                }
                
                /* Returns true if m1 is lower cost than m2. m1 and m2 must 
                   have been instantiated with the same optimization objective */
                bool operator()(const Motion* m1, const Motion* m2) const 
                {
                    return m1->getOptimizationObjective()->isCostBetterThan(m1->getCost(), m2->getCost());
                }
            };

            /** \brief Compute the distance between two motions as the cost 
                between their contained states. Note that for computationally 
                intensive cost functions, the cost between motions should be
                stored to avoid duplicate calculations */
            double distanceFunction(const Motion* a, const Motion* b) const
            {
                return opt_->motionCost(a->getState(), b->getState()).v;
            }

            /** \brief Free the memory allocated by this planner */
            void freeMemory(void);

            /** \brief Sample a state from the free configuration space and save
                it into the nearest neighbors data structure */
            void sampleFree(const ompl::base::PlannerTerminationCondition& ptc);

            /** \brief For each goal region, check to see if any of the sampled
                states fall within that region. If not, add a goal state from 
                that region directly into the set of vertices. In this way, FMT
                is able to find a solution, if one exists. If no sampled nodes
                are within a goal region, there would be no way for the 
                algorithm to successfully find a path to that region */
            void assureGoalIsSampled(const ompl::base::Goal *goal);

            /** \brief Compute the volume of the unit ball in a given dimension */
            double calculateUnitBallVolume(const unsigned int dimension) const;
            
            /** \brief Calculate the radius to use for nearest neighbor searches.
                This is something that the user should optimize over for a given
                application, between the minimum bound given in [L. Janson and M. 
                Pavone, "Fast Marching Trees: a Fast Marching Sampling-Based 
                Method for Optimal Motion Planning in Many Dimensions -- 
                Extended Version," International Symposium on Robotics Research, 
                2013. <a href="http://arxiv.org/pdf/1306.3532v2.pdf">
                http://arxiv.org/pdf/1306.3532v2.pdf</a>], and positive infinity */
            double calculateRadius(unsigned int dimension, unsigned int n) const;

            /** \brief Save the neighbors within a given radius of a state */
            void saveNeighborhood(Motion* m, const double r);
            
            /** \brief Complete one iteration of the main loop of the FMT* 
                algorithm: Find all nodes in set W within a radius r of the node 
                z. Attempt to connect them to their optimal cost-to-come parent 
                in set H. Remove all newly connected nodes from W and insert
                them into H. Remove motion z from H, and update z to be the 
                current lowest cost-to-come node in H */
            bool expandTreeFromNode(Motion *&z, const double r);
            
            /** \brief A binary heap for storing explored motions in 
                cost-to-come sorted order */
            typedef ompl::BinaryHeap<Motion*, MotionCompare> MotionBinHeap;
            
            /** \brief A binary heap for storing explored motions in 
                cost-to-come sorted order. The motions in H have been explored,
                yet are still close enough to the frontier of the explored set H
                to be connected to nodes in the unexplored set W */
            MotionBinHeap H_;
            
            /** \brief A map of all of the elements stored within the 
                MotionBinHeap H, used to convert between Motion* and Element* */
            std::map<Motion*, MotionBinHeap::Element*> hElements_;

            /** \brief A map linking a motion to all of the motions within a 
                distance r of that motion */
            std::map<Motion*, std::vector<Motion*> > neighborhoods_;
            
            /** \brief The number of samples to use when planning */
            unsigned int numSamples_;
            
            /** \brief The percentage of the state space that is occupied by obstacles */
            double obstacleCoverage_;
            
            /** \brief This planner uses a nearest neighbor search radius
                proportional to the lower bound for optimality derived for PRM* 
                in Section 3.3 of [S. Karaman and E. Frazzoli. "Sampling-based 
                algorithms for optimal motion planning." International Journal of 
                Robotics Research, 30(7):846–894, 2011].  The radius multiplier 
                is the multiplier for the lower bound. It must be > 1 
             */
            double radiusMultiplier_;
            
            /** \brief A nearest-neighbor datastructure containing the set of all motions */
            boost::shared_ptr< NearestNeighbors<Motion*> > nn_;

            /** \brief State sampler */
            base::StateSamplerPtr sampler_;

            /** \brief The cost objective function */
            base::OptimizationObjectivePtr opt_;
            
        };
    }
}


#endif // OMPL_GEOMETRIC_PLANNERS_FMT_
