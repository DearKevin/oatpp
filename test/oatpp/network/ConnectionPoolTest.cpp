/***************************************************************************
 *
 * Project         _____    __   ____   _      _
 *                (  _  )  /__\ (_  _)_| |_  _| |_
 *                 )(_)(  /(__)\  )( (_   _)(_   _)
 *                (_____)(__)(__)(__)  |_|    |_|
 *
 *
 * Copyright 2018-present, Leonid Stryzhevskyi <lganzzzo@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************/

#include "ConnectionPoolTest.hpp"

#include "oatpp/network/ConnectionPool.hpp"
#include "oatpp/core/async/Executor.hpp"

namespace oatpp { namespace test { namespace network {

namespace {

typedef oatpp::network::ConnectionPool ConnectionPool;

class StubStream : public oatpp::data::stream::IOStream, public oatpp::base::Countable {
public:

  data::v_io_size write(const void *buff, data::v_io_size count) override {
    throw std::runtime_error("It's a stub!");
  }

  data::v_io_size read(void *buff, data::v_io_size count) override {
    throw std::runtime_error("It's a stub!");
  }

  oatpp::async::Action suggestOutputStreamAction(data::v_io_size ioResult) override {
    throw std::runtime_error("It's a stub!");
  }

  oatpp::async::Action suggestInputStreamAction(data::v_io_size ioResult) override {
    throw std::runtime_error("It's a stub!");
  }

  void setOutputStreamIOMode(oatpp::data::stream::IOMode ioMode) override {
    throw std::runtime_error("It's a stub!");
  }

  oatpp::data::stream::IOMode getOutputStreamIOMode() override {
    throw std::runtime_error("It's a stub!");
  }

  void setInputStreamIOMode(oatpp::data::stream::IOMode ioMode) override {
    throw std::runtime_error("It's a stub!");
  }

  oatpp::data::stream::IOMode getInputStreamIOMode() override {
    throw std::runtime_error("It's a stub!");
  }

};

class StubStreamProvider : public oatpp::network::ConnectionProvider {
public:

  StubStreamProvider()
    : counter(0)
  {}

  std::atomic<v_int64> counter;

  std::shared_ptr<IOStream> getConnection() override {
    ++ counter;
    return std::make_shared<StubStream>();
  }

  oatpp::async::CoroutineStarterForResult<const std::shared_ptr<oatpp::data::stream::IOStream>&> getConnectionAsync() override {

    class ConnectionCoroutine : public oatpp::async::CoroutineWithResult<ConnectionCoroutine, const std::shared_ptr<oatpp::data::stream::IOStream>&> {
    public:

      Action act() override {
        return _return(std::make_shared<StubStream>());
      }

    };

    ++ counter;
    return ConnectionCoroutine::startForResult();

  }

  void close() override {
    // DO NOTHING
  }

  void invalidateConnection(const std::shared_ptr<IOStream>& connection) override {
    (void)connection;
    // DO Nothing.
  }

};

class ClientCoroutine : public oatpp::async::Coroutine<ClientCoroutine> {
private:
  std::shared_ptr<ConnectionPool> m_pool;
  std::shared_ptr<ConnectionPool::ConnectionWrapper> m_connection;
  v_int32 m_repeats;
  bool m_invalidate;
public:

  ClientCoroutine(const std::shared_ptr<ConnectionPool>& pool, bool invalidate)
    : m_pool(pool)
    , m_repeats(0)
    , m_invalidate(invalidate)
  {}

  Action act() override {
    return m_pool->getConnectionAsync().callbackTo(&ClientCoroutine::onConnection);
  }

  Action onConnection(const std::shared_ptr<ConnectionPool::ConnectionWrapper>& connection) {
    m_connection = connection;
    return yieldTo(&ClientCoroutine::useConnection);
  }

  Action useConnection() {
    if(m_repeats < 1) {
      m_repeats ++;
      return waitFor(std::chrono::milliseconds(100)).next(yieldTo(&ClientCoroutine::useConnection));
    }
    if(m_invalidate) {
      m_connection->invalidate();
    }
    return finish();
  }

};

void clientMethod(std::shared_ptr<ConnectionPool> pool, bool invalidate) {
  auto connection = pool->getConnection();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  if(invalidate) {
    connection->invalidate();
  }
}

}

void ConnectionPoolTest::onRun() {

  oatpp::async::Executor executor(1, 1, 1);

  auto connectionProvider = std::make_shared<StubStreamProvider>();
  auto pool = std::make_shared<ConnectionPool>(connectionProvider, 10 /* maxConnections */, std::chrono::seconds(10) /* maxConnectionTTL */);

  std::list<std::thread> threads;

  for(v_int32 i = 0; i < 100; i ++ ) {

    threads.push_back(std::thread(clientMethod, pool, false));
    executor.execute<ClientCoroutine>(pool, false);

  }

  for(std::thread& thread : threads) {
    thread.join();
  }

  executor.waitTasksFinished();

  OATPP_LOGD(TAG, "connections_counter=%d", connectionProvider->counter.load());
  OATPP_ASSERT(connectionProvider->counter <= 10);

  pool->close();

  executor.stop();
  executor.join();

  /* wait pool cleanup task exit */
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

}

}}}
