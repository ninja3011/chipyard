name := "doom-boom"
version := "1.0"

scalaVersion := "2.13.10"

resolvers ++= Seq(
  Resolver.sonatypeRepo("snapshots"),
  Resolver.sonatypeRepo("releases"),
  Resolver.url("berkeley-baz", url("https://berkeley-baz.eecs.berkeley.edu/artifactory/repo"))(
    Resolver.mavenStylePatterns
  )
)

libraryDependencies ++= Seq(
  "edu.berkeley.cs" %% "boom" % "1.4",
  "edu.berkeley.cs" %% "rocketchip" % "1.4",
  "edu.berkeley.cs" %% "chisel3" % "6.7.0"
)

scalacOptions := Seq(
  "-Xsource:2.13",
  "-language:reflectiveCalls",
  "-deprecation",
  "-unchecked",
  "-Xlint"
)
