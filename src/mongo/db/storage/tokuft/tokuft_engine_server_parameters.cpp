// tokuft_engine_server_parameters.cpp

/*======
This file is part of Percona Server for MongoDB.

Copyright (c) 2006, 2015, Percona and/or its affiliates. All rights reserved.

    Percona Server for MongoDB is free software: you can redistribute
    it and/or modify it under the terms of the GNU Affero General
    Public License, version 3, as published by the Free Software
    Foundation.

    Percona Server for MongoDB is distributed in the hope that it will
    be useful, but WITHOUT ANY WARRANTY; without even the implied
    warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public
    License along with Percona Server for MongoDB.  If not, see
    <http://www.gnu.org/licenses/>.
======= */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kStorage

#include "mongo/base/parse_number.h"
#include "mongo/base/status.h"
#include "mongo/util/log.h"
#include "mongo/db/server_parameters.h"
#include "mongo/db/storage/tokuft/tokuft_engine.h"
#include "mongo/db/storage/tokuft/tokuft_engine_global_accessor.h"
#include "mongo/db/storage/tokuft/tokuft_errors.h"
#include "mongo/db/storage/tokuft/tokuft_global_options.h"

namespace mongo {

    /**
     * Implements a ServerParameter that must be of integer type and has a validation and
     * modification phase (the modification will usually call into tokuft).
     */
    template<typename ValueType>
    class TokuFTEngineServerParameter : public ServerParameter {
        std::string _shortName;
        ValueType& _globalOptionValue;

        Status adjust(long long newValue) const {
            Status s = check(newValue);
            if (!s.isOK()) {
                return s;
            }

            s = modify(static_cast<ValueType>(newValue));
            if (!s.isOK()) {
                return s;
            }

            _globalOptionValue = static_cast<ValueType>(newValue);
            return Status::OK();
        }

    protected:
        virtual Status check(long long newValue) const = 0;

        virtual Status modify(ValueType newValue) const = 0;

    public:
        TokuFTEngineServerParameter(const std::string& shortName, ValueType& globalOptionValue)
            : ServerParameter(ServerParameterSet::getGlobal(), shortName,
                              false, // allowedToChangeAtStartup
                              true   // allowedToChangeAtRuntime
                              ),
              _shortName(shortName),
              _globalOptionValue(globalOptionValue)
        {}

        virtual void append(OperationContext* txn, BSONObjBuilder& b, const std::string& name) {
            b << name << _globalOptionValue;
        }

        virtual Status set(const BSONElement& newValueElement) {
            long long newValue;
            if (!newValueElement.isNumber()) {
                StringBuilder sb;
                sb << "Expected number type for " << _shortName << " via setParameter command: "
                   << newValueElement;
                return Status(ErrorCodes::BadValue, sb.str());
            }
            if (newValueElement.type() == NumberDouble &&
                (newValueElement.numberDouble() - newValueElement.numberLong()) > 0) {
                StringBuilder sb;
                sb << _shortName << " must be a whole number: "
                   << newValueElement;
                return Status(ErrorCodes::BadValue, sb.str());
            }
            newValue = newValueElement.numberLong();

            return adjust(newValue);
        }

        virtual Status setFromString(const std::string& str) {
            unsigned newValue;
            Status status = parseNumberFromString(str, &newValue);
            if (!status.isOK()) {
                return status;
            }

            return adjust(newValue);
        }
    };

    class TokuFTEngineCheckpointPeriodSetting : public TokuFTEngineServerParameter<int> {
    public:
        TokuFTEngineCheckpointPeriodSetting()
            : TokuFTEngineServerParameter("PerconaFTEngineCheckpointPeriod",
                                          tokuftGlobalOptions.engineOptions.checkpointPeriod)
        {}

    protected:
        Status check(long long newValue) const {
            if (newValue <= 0) {
                StringBuilder sb;
                sb << "PerconaFTEngineCheckpointPeriod must be > 0, but attempted to set to: "
                   << newValue;
                return Status(ErrorCodes::BadValue, sb.str());
            }
            return Status::OK();
        }

        Status modify(int newValue) const {
            return statusFromTokuFTError(tokuftGlobalEnv().checkpointing_set_period(newValue));
        }
    } tokuftEngineCheckpointPeriodSetting;

    class TokuFTEngineCleanerIterationsSetting : public TokuFTEngineServerParameter<int> {
    public:
        TokuFTEngineCleanerIterationsSetting()
            : TokuFTEngineServerParameter("PerconaFTEngineCleanerIterations",
                                          tokuftGlobalOptions.engineOptions.cleanerIterations)
        {}

    protected:
        Status check(long long newValue) const {
            if (newValue < 0) {
                StringBuilder sb;
                sb << "PerconaFTEngineCleanerIterations must be >= 0, but attempted to set to: "
                   << newValue;
                return Status(ErrorCodes::BadValue, sb.str());
            }
            return Status::OK();
        }

        Status modify(int newValue) const {
            return statusFromTokuFTError(tokuftGlobalEnv().cleaner_set_iterations(newValue));
        }
    } tokuftEngineCleanerIterationsSetting;

    class TokuFTEngineCleanerPeriodSetting : public TokuFTEngineServerParameter<int> {
    public:
        TokuFTEngineCleanerPeriodSetting()
            : TokuFTEngineServerParameter("PerconaFTEngineCleanerPeriod",
                                          tokuftGlobalOptions.engineOptions.cleanerPeriod)
        {}

    protected:
        Status check(long long newValue) const {
            if (newValue < 0) {
                StringBuilder sb;
                sb << "PerconaFTEngineCleanerPeriod must be >= 0, but attempted to set to: "
                   << newValue;
                return Status(ErrorCodes::BadValue, sb.str());
            }
            return Status::OK();
        }

        Status modify(int newValue) const {
            return statusFromTokuFTError(tokuftGlobalEnv().cleaner_set_period(newValue));
        }
    } tokuftEngineCleanerPeriodSetting;

    class TokuFTEngineLockTimeoutSetting : public TokuFTEngineServerParameter<int> {
    public:
        TokuFTEngineLockTimeoutSetting()
            : TokuFTEngineServerParameter("PerconaFTEngineLockTimeout",
                                          tokuftGlobalOptions.engineOptions.lockTimeout)
        {}

    protected:
        Status check(long long newValue) const {
            if (newValue < 0 || newValue > 60000) {
                StringBuilder sb;
                sb << "PerconaFTEngineLockTimeout must be between 0 and 60000, but attempted to set to: "
                   << newValue;
                return Status(ErrorCodes::BadValue, sb.str());
            }
            return Status::OK();
        }

        Status modify(int newValue) const {
            return statusFromTokuFTError(tokuftGlobalEnv().change_fsync_log_period(newValue));
        }
    } tokuftEngineLockTimeoutSetting;

    class TokuFTEngineJournalCommitIntervalSetting : public TokuFTEngineServerParameter<int> {
    public:
        TokuFTEngineJournalCommitIntervalSetting()
            : TokuFTEngineServerParameter("PerconaFTEngineJournalCommitInterval",
                                          tokuftGlobalOptions.engineOptions.journalCommitInterval)
        {}

    protected:
        Status check(long long newValue) const {
            if (newValue <= 0 || newValue > 300) {
                StringBuilder sb;
                sb << "PerconaFTEngineJournalCommitInterval must be between 1 and 300, but attempted to set to: "
                   << newValue;
                return Status(ErrorCodes::BadValue, sb.str());
            }
            return Status::OK();
        }

        Status modify(int newValue) const {
            return statusFromTokuFTError(tokuftGlobalEnv().change_fsync_log_period(newValue));
        }
    } tokuftEngineJournalCommitIntervalSetting;

}
